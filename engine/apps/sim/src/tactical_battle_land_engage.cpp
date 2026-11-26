// tactical_battle: cmb1 t01 / t03 - the land formation records (LandFormationRecord), land Auto_Engage and the land scan
// range (legacy docs/re/BKR1_RE_ANSWERS.md Q2 / Q8 [H], FIGHTER_FLYING §4.10 A / §4.11 B / E [H]).
#include <optional>

#include "alamo/tactical/movement/formation_stack.hpp"
#include "tactical_battle_internal.hpp"

namespace alamo::sim {
using namespace bo3split;

namespace {
// A unit of this battle by id, const (units_ is sorted by id).
template <class Units>
auto unit_in(const Units& units, objects::ObjectId id) -> const typename Units::value_type* {
    const auto it = std::lower_bound(units.begin(), units.end(), id, [](const auto& x, objects::ObjectId v) { return x.id < v; });
    return it == units.end() || it->id != id ? nullptr : &*it;
}
}  // namespace

void TacticalBattle::land_order(std::span<const objects::ObjectId> ids, uint8_t flags, Vec3 goal, objects::ObjectId goal_object) {
    if (space_ || !movement_) return;
    for (const objects::ObjectId id : ids) {
        const Unit* u = unit_by_id(id);
        if (!u || !u->alive || u->retreated || !u->movable || movement_locked(id)) continue;  // a locked unit keeps its formation
        const objects::ObjectId mover = attack_mover(id);
        if (!movement_->has_unit(mover)) continue;
        auto it = std::lower_bound(lformations_.begin(), lformations_.end(), mover,
                                   [](const LandFormationRecord& r, objects::ObjectId v) { return r.mover < v; });
        if (it == lformations_.end() || it->mover != mover) it = lformations_.insert(it, LandFormationRecord{});
        LandFormationRecord nr;
        nr.mover = mover;
        mv::FighterBlock b;  // #488: the order's block, Set_Move(F, b, clear) - a new coordinator: the old stack (and its
                             // auto block) is gone (FIGHTER_FLYING §4.11 B 3)
        b.kind = goal_object != objects::kNoObjectId ? mv::kFighterBlockObject : mv::kFighterBlockPosition;
        b.object = goal_object;
        b.point = {goal.x, goal.y};
        // the order flags and a player order's 0x04 (#489: the patrol loop closes on it, as on the fighters' order block)
        b.flags = static_cast<uint8_t>(flags & (LandFormationRecord::kOrderFlags | mv::kMoveFlagPlayerOrder));
        (void)mv::stack_set_move(nr.blocks, b, true);
        nr.goal_z = goal.z;
        nr.seen = goal;
        *it = std::move(nr);
    }
}

void TacticalBattle::land_order_clear(std::span<const objects::ObjectId> ids) {
    for (const objects::ObjectId id : ids) {
        const objects::ObjectId mover = attack_mover(id);
        std::erase_if(lformations_, [mover](const LandFormationRecord& r) { return r.mover == mover; });
    }
}

const LandFormationRecord* TacticalBattle::land_formation(objects::ObjectId mover) const {
    const auto it = std::lower_bound(lformations_.begin(), lformations_.end(), mover,
                                     [](const LandFormationRecord& r, objects::ObjectId v) { return r.mover < v; });
    return it != lformations_.end() && it->mover == mover ? &*it : nullptr;
}

bool TacticalBattle::land_auto_pushed(objects::ObjectId unit) const {
    if (space_ || lformations_.empty()) return false;
    const LandFormationRecord* r = land_formation(attack_mover(unit));
    return r && r->pushed() != objects::kNoObjectId;
}

std::vector<objects::ObjectId> TacticalBattle::land_mover_units(objects::ObjectId mover) const {
    std::vector<objects::ObjectId> out;
    if (movement_ && movement_->is_land_team_container(mover)) {
        for (const auto& [mid, slot] : movement_->land_team_slots(mover)) {
            (void)slot;
            const Unit* m = unit_in(units_, mid);
            if (m && m->alive && !m->retreated) out.push_back(mid);
        }
    } else {
        out.push_back(mover);
    }
    return out;
}

objects::ObjectId TacticalBattle::land_mover_target_unit(objects::ObjectId mover) {
    // Service_Land runs on the TARGETING owner: a land team container's own TeamTargeting, else the unit (BKR1 Q8: on land
    // the owner is not replaced by its container)
    if (combat_->has_unit(mover)) return mover;
    return attack_lead(mover);
}

const TacticalBattle::Unit* TacticalBattle::land_goal_unit(const objects::ObjectStore& st, objects::ObjectId id) const {
    const auto live = [&](objects::ObjectId x) -> const Unit* {
        const Unit* u = x != objects::kNoObjectId ? unit_in(units_, x) : nullptr;
        return u && u->alive && !u->retreated && st.alive(u->h) ? u : nullptr;
    };
    if (const Unit* u = live(id)) return u;
    if (id == objects::kNoObjectId || !movement_->is_land_team_container(id)) return nullptr;
    for (const auto& [mid, slot] : movement_->land_team_slots(id)) {
        (void)slot;
        if (const Unit* m = live(mid)) return m;
    }
    return nullptr;
}

bool TacticalBattle::land_goal_member(objects::ObjectId goal, objects::ObjectId id) const {
    if (goal == objects::kNoObjectId || id == objects::kNoObjectId) return false;
    if (id == goal) return true;
    if (!movement_->is_land_team_container(goal)) return false;
    for (const auto& [mid, slot] : movement_->land_team_slots(goal)) {
        (void)slot;
        if (mid == id) return true;
    }
    return false;
}

uint32_t TacticalBattle::land_type_of(objects::ObjectId mover) const {
    if (const Unit* u = unit_in(units_, mover)) return u->type;
    for (const Container& c : containers_)  // u +0x337 = -1: the company type 0x3973b0 (the container's own type without one)
        if (c.id == mover && !c.removed) return c.squad_type != 0xFFFFFFFFu ? c.squad_type : c.type;
    return 0xFFFFFFFFu;
}

// 0x4160a0(F, u, c, &Rr) on land: FIGHTER_FLYING §4.10 A without the Space_Layer gate (BKR1 Q8 [H]); the rules are
// fighter_chase_test's. [port] F state 3 ("arrived") is the mover's own order done (UnitView::has_goal false); F's current
// path point of the attack-move branch is not modelled ([M] the unit answers 2 while its attack-move is under way); the slot
// goal is the mover's movement goal (an object block: the object's position).
uint8_t TacticalBattle::land_chase(const objects::ObjectStore& store, const LandFormationRecord& r, objects::ObjectId unit,
                                   objects::ObjectId cand, float range, float& rr) const {
    rr = 0.0f;
    if (r.locked || !movement_->has_unit(r.mover)) return 0;  // u order-locked (0x39ad40)
    const mv::UnitView v = movement_->view(r.mover);
    if (v.deploy_state != mv::LocoState::None) return 0;  // deployed with type +0x23b0 = 0 [port: any deploy state]
    const auto live = [&](objects::ObjectId id) -> const Unit* {
        const Unit* x = id != objects::kNoObjectId ? unit_in(units_, id) : nullptr;
        return x && x->alive && !x->retreated && store.alive(x->h) ? x : nullptr;
    };
    // the land adapter of the one chase-input builder (tactical_battle_chase.cpp)
    ChaseOrder o;
    o.first_flags = o.flags = r.flags();  // Rr and the attack-move branch read blocks[0]
    o.moving = v.has_goal;
    o.live_push = r.pushed() != objects::kNoObjectId;
    o.goal_object = r.goal_object() != objects::kNoObjectId;
    // order_goal_live stays false: BKR1 Q8 / FIGHTER_FLYING §4.10 A read the block objects only; the fighter adapter's
    // combat order goal is the space port's stand-in for an attack order's object (land records carry it in goal_object).
    // a guarded TEAM container: its own mover, its first live member's position
    const Unit* guarded = (r.flags() & mv::kMoveFlagGuardMove) != 0 ? land_goal_unit(store, r.goal_object()) : nullptr;
    o.guarded_mover = guarded && movement_->has_unit(r.goal_object()) ? r.goal_object() : guarded ? guarded->id : objects::kNoObjectId;
    o.unit = Vec3{v.pos.x, v.pos.y, 0.0f};
    o.goal = Vec3{v.goal.x, v.goal.y, 0.0f};
    if (const Unit* go = land_goal_unit(store, r.goal_object())) o.goal = store.position(go->h);
    o.range_type = land_type_of(r.mover);
    o.attack_range = range;
    FighterChaseInputs ci = chase_inputs(o);
    if (cand == objects::kNoObjectId) {
        ci.candidate = ci.goal;
    } else {
        const Unit* c = live(cand);
        if (!c) return 0;
        ci.candidate_is_goal = land_goal_member(r.goal_object(), cand) || cand == r.pushed();  // c is the object of the first or the last block
        // c attacks the guarded object or u itself (0x39b1c0: its attacker list; [port] its current combat target)
        const objects::ObjectId at = combat_->has_unit(c->id) ? combat_->current_target(c->id) : objects::kNoObjectId;
        if (at != objects::kNoObjectId) {
            if (guarded && land_goal_member(r.goal_object(), at)) ci.candidate_attacker = true;
            if (at == unit || at == r.mover) ci.candidate_attacker = true;
            if (!ci.candidate_attacker && movement_->is_land_team_container(r.mover))
                for (const auto& [mid, slot] : movement_->land_team_slots(r.mover)) {
                    (void)slot;
                    if (at == mid) ci.candidate_attacker = true;
                }
        }
        ci.candidate = store.position(c->h);
    }
    return chase_answer(ci, rr);
}

uint8_t TacticalBattle::land_scan_range(const objects::ObjectStore& store, objects::ObjectId unit, objects::ObjectId cand, float range,
                                        float& rr) const {
    rr = 0.0f;
    const LandFormationRecord* r = land_formation(attack_mover(unit));
    return r ? land_chase(store, *r, unit, cand, range, rr) : uint8_t{0};
}

void TacticalBattle::land_engage_service(TacticalMode& mode, SimRng& rng) {
    if (space_ || !movement_ || !combat_) return;
    const objects::ObjectStore& st = mode.primary();
    const auto live = [&](objects::ObjectId id) -> const Unit* {
        const Unit* x = id != objects::kNoObjectId ? unit_by_id(id) : nullptr;
        return x && x->alive && !x->retreated && st.alive(x->h) && combat_->has_unit(x->id) ? x : nullptr;
    };
    // the records of movers that are gone (death, retreat, a squad emptied)
    std::erase_if(lformations_, [&](const LandFormationRecord& r) {
        if (!movement_->has_unit(r.mover)) return true;
        if (movement_->is_land_team_container(r.mover)) return land_mover_units(r.mover).empty();
        const Unit* u = unit_by_id(r.mover);
        return !u || !u->alive || u->retreated;
    });
    // derived per tick (serial): the order lock of the members (0x39ad40; Push 0x41fd30 refuses any locked member) and the
    // chase ranges of each record's type (read by the parallel scan)
    for (LandFormationRecord& r : lformations_) {
        r.locked = false;
        for (const objects::ObjectId id : land_mover_units(r.mover)) r.locked = r.locked || movement_locked(id);
        chase_ranges_fill(land_type_of(r.mover));
    }
    land_waypoints_drop_locked();  // #489: before the arrival pops (land_waypoint_service) walk a locked mover on
    // cmb1 t03 (Get_Scan_Range 0x566f50 on land, BKR1 Q8 [H]): the TARGETING owner o (a land team container, else the unit)
    // asks the formation (kUnitScanQuery, land_scan_range) when it has one and is not deployed; else R (steps 1 / 2). [port]
    // a steering squad's TeamTargeting runs on its head member (from the container's position): its members carry the flag.
    for (const Unit& u : units_) {
        if (!combat_->has_unit(u.id)) continue;
        bool q = false;
        const objects::ObjectId mover = attack_mover(u.id);
        if (u.alive && !u.retreated && movement_->has_unit(mover))
            q = land_formation(mover) != nullptr && movement_->view(mover).deploy_state == mv::LocoState::None;
        combat_->set_flag(u.id, tc::kUnitScanQuery, q);
    }
    for (const Container& c : containers_) {
        if (c.removed || !combat_->has_unit(c.id)) continue;
        combat_->set_flag(c.id, tc::kUnitScanQuery, movement_->has_unit(c.id) && land_formation(c.id) != nullptr);
    }
    // cmb2 t07 (BKR2_RE_ANSWERS Q3 [H]): MovingVsStopped step 10 (0x685490) found a hostile unit on the mover's
    // probe detour - its formation F gets the kind-3 attack block on it (flags 0, +0x50 = 1, Push 0x41fd30: the attack slots
    // and the plan), so the whole formation switches to attacking it. F null (no land formation record) -> nothing
    // ([port] the movement already resolved the pair). The pairs in the movement's order (its serial tick).
    for (const mv::MovementSystem::FormationAttack& fa : movement_->formation_attacks()) (void)land_step10_attack(mode, fa.mover, fa.target, rng);
    // Per record, ascending mover (land_attack_order / land_pop do not touch lformations_).
    for (size_t i = 0; i < lformations_.size(); ++i) {
        LandFormationRecord& r = lformations_[i];
        // #510 state 6 (0x422b70 [H]): while any member's locomotor moves, wait; then wait while frame <= deadline; then
        // speed := -1, state 0 (the re-plan)
        if (r.wait && land_members_still(r) && frame_ > r.deadline) {
            r.wait = false;
            land_replan(mode, r, rng);
        }
        const objects::ObjectId tu = land_mover_target_unit(r.mover);
        const Unit* lead = live(attack_lead(r.mover));
        if (tu == objects::kNoObjectId || !combat_->has_unit(tu) || !lead) continue;
        const mv::UnitView v = movement_->view(r.mover);
        const int32_t me = st.owner(lead->h);
        // blocks[0]'s last-observed point (0x584750 at every formation step, the lead as viewer): seen ->
        // the object's position; hidden -> frozen; stealthed -> the formation lead's position, which is the member nearest
        // the movers' centroid and is taken where the movers are known (BattleAi::guard_follow_step)
        if (const Unit* go = land_goal_unit(st, r.goal_object())) {
            bool hidden = false, stealthed = false;
            attack_target_unseen(st, *lead, go->id, hidden, stealthed);
            if (!hidden && !stealthed) r.seen = st.position(go->h);
        }
        // 1. The auto block on top (n = 2). FIGHTER_FLYING §4.11 B 3 / E [H]: while it is the top, 0x4192b0 (moving, state 1)
        //    re-paths it only when 0x4165f0 holds (the leash), else step 5 pops it (no 0x04); 0x410a60 (arrived, state 3)
        //    pops it when the leash fails and (c is false or blocks[0] names an object). Its object's death ends it too
        //    ([port] at once; retail converts it to a position at its path end and pops it there on arrival).
        if (r.pushed() != objects::kNoObjectId) {
            const Unit* pu = live(r.pushed());
            bool pop = !pu || !combat_->is_enemy(me, st.owner(pu->h));
            if (!pop) {  // the attack block that carries it is gone (a C 3 drop, a newer block)
                const auto it = std::lower_bound(attack_blocks_.begin(), attack_blocks_.end(), r.mover,
                                                 [](const AttackBlock& b, objects::ObjectId m) { return b.mover < m; });
                pop = it == attack_blocks_.end() || it->mover != r.mover || it->target != r.pushed();
            }
            if (!pop && frame_ >= r.next) {
                r.next = frame_ + 30u;
                bool hidden = false, stealthed = false;
                attack_target_unseen(st, *lead, r.pushed(), hidden, stealthed);
                // 0x4165f0 for n >= 2 (§4.11 E): blocks[0] attack-move -> Attack_Move_Response_Range around F's path point
                // ([port] the formation's own position: the lead stays inside); else Guard / Idle_Chase_Range around the
                // goal of blocks[n-2] (the order's goal point). The attacker extension; a moving guarded object answers its
                // attackers only; the object farther than Rr from the anchor -> the 0x5672b0 point test ([port] within
                // Rr + the attack range, as fighter_chase_test). A hidden object -> false.
                const uint32_t ty = land_type_of(r.mover);
                const bool am = (r.flags() & mv::kMoveFlagAttackMove) != 0;
                const std::array<float, 4> cr = chase_ranges(ty);  // filled above for the record's type
                float rr = am ? cr[2] : (r.flags() & mv::kMoveFlagGuardMove) != 0 ? cr[0] : cr[1];
                const float ext = cr[3];
                if (rr < 0.0f) rr = 0.0f;
                const Unit* go = land_goal_unit(st, r.goal_object());
                Vec3 anchor = am ? Vec3{v.pos.x, v.pos.y, 0.0f} : go ? st.position(go->h) : r.goal();
                const objects::ObjectId at = combat_->current_target(pu->id);
                const std::vector<objects::ObjectId> us = land_mover_units(r.mover);
                const bool attacker = at != objects::kNoObjectId && ((go && (r.flags() & mv::kMoveFlagGuardMove) && land_goal_member(r.goal_object(), at)) || at == r.mover ||
                                                                    std::find(us.begin(), us.end(), at) != us.end());
                if (attacker) rr += ext;
                bool a = !hidden;
                const objects::ObjectId gm = go && movement_->has_unit(r.goal_object()) ? r.goal_object() : go ? go->id : objects::kNoObjectId;
                if (a && go && (r.flags() & mv::kMoveFlagGuardMove) && !attacker && movement_->has_unit(gm)) {
                    const mv::UnitView gv = movement_->view(gm);
                    if (gv.has_goal && gv.speed > 0.0f) a = false;
                }
                const auto d2 = [](Vec3 p, Vec3 q) { return (p.x - q.x) * (p.x - q.x) + (p.y - q.y) * (p.y - q.y); };
                const Vec3 L{v.pos.x, v.pos.y, 0.0f}, cp = st.position(pu->h);
                if (a && !(d2(L, anchor) < rr * rr)) a = false;
                if (a && d2(cp, anchor) > rr * rr) {
                    const float far = rr + combat_->attack_range(tu);
                    a = d2(cp, anchor) <= far * far;
                }
                if (v.has_goal) {
                    pop = !a;
                } else {  // 0x40edd0: the mover tested through its lead (a squad container: its first live member, D 5)
                    bool c = !hidden && !stealthed;
                    if (c) {
                        const tc::CanHit h = attack_can_hit(lead->id, r.pushed());
                        c = h == tc::CanHit::Yes || h == tc::CanHit::NoTarget;
                    }
                    pop = !a && (!c || r.goal_object() != objects::kNoObjectId);
                }
            }
            if (pop) {
                land_pop(mode, r, rng);
                continue;
            }
        }
        // 2. Auto_Engage (Service_Land 0x634810 -> locomotor vf +0x260 for every service with a target, locked or not):
        //    LocomotorCommon 0x42d0f0 for Walk / Bike / LandTeamContainer / LandTeamInfantry ([port] JetPack's own 0x6418f0
        //    and the air locomotors are not read: they do not push). F's goal 0x41a2e0(F, -1) = the newest block with a
        //    hostile object, never a guard; t != goal and rr = 0x4160a0(F, u, t) != 0 -> the auto block {kind 3, t, flags 0,
        //    +0x5d = 1} ([port] rr = 1's split into a formation of its own: the record is per mover already).
        if (r.locked) continue;
        if (v.locomotor != mv::LocomotorKind::Default && v.locomotor != mv::LocomotorKind::Bike) continue;
        const objects::ObjectId t = combat_->current_target(tu);
        const Unit* tt = live(t);
        if (!tt || !combat_->is_enemy(me, st.owner(tt->h))) continue;
        const objects::ObjectId goal =
            r.pushed() != objects::kNoObjectId ? r.pushed() : (r.flags() & mv::kMoveFlagGuardMove) != 0 ? objects::kNoObjectId : r.goal_object();
        if (t == goal || (r.pushed() == objects::kNoObjectId && land_goal_member(goal, t))) continue;
        float rr = 0.0f;
        if (land_chase(st, r, tu, t, combat_->attack_range(tu), rr) == 0) continue;
        if (land_mover_units(r.mover).empty()) continue;
        land_push(mode, r, tu, *tt, rng);
        ++stats_.land_auto_pushes;
    }
}

bool TacticalBattle::land_step10_attack(TacticalMode& mode, objects::ObjectId mover, objects::ObjectId target, SimRng& rng) {
    if (space_ || !movement_ || !combat_) return false;
    const objects::ObjectStore& st = mode.primary();
    const auto live = [&](objects::ObjectId id) -> const Unit* {
        const Unit* x = id != objects::kNoObjectId ? unit_by_id(id) : nullptr;
        return x && x->alive && !x->retreated && st.alive(x->h) && combat_->has_unit(x->id) ? x : nullptr;
    };
    const auto it = std::lower_bound(lformations_.begin(), lformations_.end(), mover,
                                     [](const LandFormationRecord& x, objects::ObjectId m) { return x.mover < m; });
    if (it == lformations_.end() || it->mover != mover || it->locked || it->pushed() == target) return false;
    const Unit* tt = live(target);
    const objects::ObjectId tu = land_mover_target_unit(mover);
    const Unit* lu = live(attack_lead(mover));
    if (!tt || !lu || tu == objects::kNoObjectId || !combat_->has_unit(tu)) return false;
    if (!combat_->is_enemy(st.owner(lu->h), st.owner(tt->h))) return false;
    land_push(mode, *it, tu, *tt, rng);
    ++stats_.land_step10_pushes;
    return true;
}

// Push 0x41fd30: the block goes on top (the pre-pop drops an earlier auto block, never the order's own); the formation goal
// becomes t (path B locks it in weapon range: the units' attack goal); the members plan to it (land_attack_order; the
// AttackBlock with kAttackBlockNoLock carries its 30-frame checks: no unit's order names it).
void TacticalBattle::land_push(TacticalMode& mode, LandFormationRecord& r, objects::ObjectId tu, const Unit& t, SimRng& rng) {
    const std::vector<objects::ObjectId> us = land_mover_units(r.mover);
    if (us.empty()) return;
    r.wait = false;  // #510: a Push plans to the new top (state 0); a state-6 wait ends
    const Vec3 tp = mode.primary().position(t.h);
    // cmb2 t04 (FIGHTER_FLYING §4.11 B, BKT1_ATTACK_RE §1.3 [H]): Push gives the block a hard point when the object has hard
    // points (0x394610 Choose_Target_Hardpoint for the TARGETING unit; -1 none)
    const int32_t hp = combat_->hardpoint_count(t.id) > 0 ? combat_->choose_target_hardpoint(tu, t.id) : -1;
    mv::FighterBlock b;  // #488: {kind 3, t, hard point, flags 0, +0x50 the auto class, +0x5d = 1} through the shared Push
    b.kind = mv::kFighterBlockObject;
    b.object = t.id;
    b.hard_point = hp;
    b.pushed = true;
    b.own_speed = true;
    mv::stack_push(r.blocks, b);
    r.next = frame_ + 30u;
    for (const objects::ObjectId id : us)
        if (combat_->has_unit(id)) combat_->set_attack_goal(id, t.id, hp);
    attack_block_set(us.front(), t.id, kAttackBlockNoLock, tp, hp);
    (void)land_attack_order(mode, us, t.id, tp, 0, false, rng);
}

// 0x4105a0 of the auto block (FIGHTER_FLYING §4.11 B 1): the order's own block is the top again and the formation plans to
// it (state 5 / 0 [M]): an object block (attack / guard) re-plans on its object, a position block moves to its point. The
// formation goal (path B) returns to the order's object (a guard: none). [port] the members' combat targets are left alone.
void TacticalBattle::land_pop(TacticalMode& mode, LandFormationRecord& r, SimRng& rng) {
    const std::optional<mv::FighterBlock> popped = mv::stack_pop(r.blocks);  // #488: the shared Pop (the auto block on top)
    const objects::ObjectId was = popped ? popped->object : objects::kNoObjectId;
    ++stats_.land_auto_pops;
    // the object listener goes when no block left names T's object
    const bool named = std::any_of(r.blocks.begin(), r.blocks.end(), [&](const mv::FighterBlock& b) {
        return b.kind == mv::kFighterBlockObject && b.object == was;
    });
    const auto it = std::lower_bound(attack_blocks_.begin(), attack_blocks_.end(), r.mover,
                                     [](const AttackBlock& b, objects::ObjectId m) { return b.mover < m; });
    if (!named && it != attack_blocks_.end() && it->mover == r.mover && it->target == was) attack_blocks_.erase(it);
    if (popped) land_pop_effects(*popped, r.mover);
    if (r.blocks.empty()) return;
    // #510: Pop 0x4105a0 goes to state 5 after an object block (kind 3), else to state 0 (the re-plan)
    if (popped && popped->kind == mv::kFighterBlockObject && !land_pop_ready(r)) return;
    land_replan(mode, r, rng);
}

// #510 state 5, 0x422e50 [H] (read 2026-11-26): the deadline F +0x244 := frame + 2, raised to each member's TARGETING
// (behaviour 9) vf +0x110 - 0x566b80, the targeting data's +0x20, which 0x5670b0 sets to frame + interval + rand(0,
// interval / 2): CombatSystem::next_scan, the same rule (formulas next_scan_frame) [H]; a team member answers through its
// own members (land_mover_units lists the units). TARGETING vf +0xe0 (0x567080: the targeting data's +8 is set) is "has a
// target": CombatSystem::current_target [M]. Every member with a target and none whose locomotor moves (vf +0x1a8 or vf
// +0x1c0(0)): speed := -1, state 0 (re-plan now). Else state 6 (r.wait).
bool TacticalBattle::land_pop_ready(LandFormationRecord& r) {
    uint32_t deadline = frame_ + 2u;
    bool targets = true;
    for (const objects::ObjectId id : land_mover_units(r.mover)) {
        if (!combat_->has_unit(id)) continue;
        deadline = std::max(deadline, combat_->next_scan(id));
        targets = targets && combat_->current_target(id) != tc::kNoObject;
    }
    r.deadline = deadline;
    r.wait = !(targets && land_members_still(r));
    if (r.wait) ++stats_.land_pop_waits;
    return !r.wait;
}

bool TacticalBattle::land_members_still(const LandFormationRecord& r) const {
    for (const objects::ObjectId id : land_mover_units(r.mover))
        if (movement_->has_unit(id) && movement_->locomotor_moving(id)) return false;
    return true;
}

// #491 (Pop 0x4105a0's member loop [H], read from the binary 2026-11-24): for each member of the formation, in member order
// (the mover's units; [port] a land TEAM container's own ability slots last):
//   - T has 0x40: signal 0x2a (0x220ed0) [port: counted - nothing in the port writes 0x40 or listens for 0x2a];
//   - a member with a layer (0x397900(m, 1) != 0: a movement unit): the orientation := its locomotor's vf +0x1e8 (0x3a8710;
//     MovementSystem::pop_facing) [port: the mode manager's vf +0x238 gate is taken as true in a battle];
//   - T lacks 0x10 (0x583670) and the member's unit ability 5, BARRAGE, is active (0x39b950): switched off (0x38a350(m, 5,
//     0)). [port] Nothing in the port writes 0x10 on a land block.
// [port] The path-progress object F +0x230 (freed by the Pop) has no port counterpart.
void TacticalBattle::land_pop_effects(const mv::FighterBlock& t, objects::ObjectId mover) {
    constexpr uint8_t kBarrage = 5;  // the unit ability table: BARRAGE
    const auto barrage_off = [&](objects::ObjectId id) {
        if ((t.flags & 0x10) == 0 && abilities_ && abilities_->has_object(id) && abilities_->is_active(id, kBarrage))
            (void)abilities_->set_unit_ability_active(id, kBarrage, false, frame_);
    };
    for (const objects::ObjectId u : land_mover_units(mover)) {
        if ((t.flags & 0x40) != 0) ++stats_.land_pop_signals;
        if (movement_ && movement_->has_unit(u))
            if (const float y = movement_->pop_facing(u); y != mv::kNoFacing && movement_->snap_yaw(u, y)) ++stats_.land_pop_facings;
        barrage_off(u);
    }
    if (movement_ && movement_->is_land_team_container(mover)) barrage_off(mover);
}

// The formation plans to its top block (state 0 -> 0x41c1a0; the recorded goal := goal(top)): an object block (attack /
// guard) re-plans on its object, a position block moves the members to its point (a waypoint keeps its own flags, not the
// stack's 0x04 / 0x08).
void TacticalBattle::land_replan(TacticalMode& mode, LandFormationRecord& r, SimRng& rng) {
    if (r.blocks.empty()) return;
    const mv::FighterBlock& top = r.blocks.back();
    const uint8_t tflags = static_cast<uint8_t>(top.flags & LandFormationRecord::kOrderFlags);
    const objects::ObjectId tobj = top.kind == mv::kFighterBlockObject ? top.object : objects::kNoObjectId;
    const Vec3 tgoal{top.point.x, top.point.y, r.goal_z};
    const std::vector<objects::ObjectId> us = land_mover_units(r.mover);
    if (us.empty()) return;
    const objects::ObjectStore& st = mode.primary();
    const objects::ObjectId goal = (tflags & mv::kMoveFlagGuardMove) != 0 ? objects::kNoObjectId : tobj;
    for (const objects::ObjectId id : us)
        if (combat_->has_unit(id)) combat_->set_attack_goal(id, goal);
    // the object (a TEAM container through its first live member's position; the order names the container)
    const Unit* go = land_goal_unit(st, tobj);
    if (go && combat_->has_unit(go->id)) {
        const Vec3 tp = st.position(go->h);
        if ((tflags & mv::kMoveFlagGuardMove) != 0) {
            (void)land_attack_order(mode, us, tobj, tp, mv::kMoveFlagGuardMove, false, rng, true);
            return;
        }
        const Unit* lu = unit_by_id(us.front());
        const bool hostile = lu && combat_->is_enemy(st.owner(lu->h), st.owner(go->h));
        attack_block_set(us.front(), tobj, static_cast<uint8_t>(tflags | (hostile ? 0 : kAttackBlockNoLock)), tp);
        (void)land_attack_order(mode, us, tobj, tp, tflags, false, rng);
        return;
    }
    // a position block (or an object block whose object is gone: its position then)
    mv::MoveOrder o;
    for (const objects::ObjectId id : us)
        if (!movement_locked(id)) o.units.push_back(id);
    if (o.units.empty()) return;
    const tactical::Aabb& bb = mode.bounds();
    o.destination = {std::clamp(tgoal.x, bb.min.x, bb.max.x), std::clamp(tgoal.y, bb.min.y, bb.max.y)};
    o.flags = static_cast<uint8_t>(top.flags & ~LandFormationRecord::kStackFlags);
    if (std::isfinite(top.facing)) o.facing_deg = top.facing;
    movement_->move(o, rng, frame_);
}
LandFormationRecord* TacticalBattle::land_formation_mut(objects::ObjectId mover) {
    return const_cast<LandFormationRecord*>(std::as_const(*this).land_formation(mover));
}

// #489 (FIGHTER_FLYING §4.11 B 1, MOVEMENT §6.2 [H]): a shift-queued land move goes through coordinator vf +0x30 to the
// waypoint append 0x420f70 of the mover's formation (the land patrol radius 20, MaxWaypointsPerPath): once per mover record
// whose running block (the top below any auto block) is a position block and whose units hold no host-queued order. The
// block is {position, the event's flags | 0x08}; the append adds 0x04. Returns the other ids, which keep the host's order
// queue. [port] A record running an attack block keeps the host queue (an attack top never pops on arrival).
std::vector<objects::ObjectId> TacticalBattle::land_waypoint_append(TacticalMode& mode, std::span<const objects::ObjectId> ids, float x,
                                                                     float y, float facing, uint8_t flags) {
    std::vector<objects::ObjectId> rest, done;
    const objects::ObjectStore& st = mode.primary();
    const tactical::Aabb& bb = mode.bounds();
    mv::FighterBlock b;
    b.kind = mv::kFighterBlockPosition;
    b.point = {std::clamp(x, bb.min.x, bb.max.x), std::clamp(y, bb.min.y, bb.max.y)};
    if (std::isfinite(facing)) b.facing = facing;
    b.flags = static_cast<uint8_t>(flags | mv::kMoveFlagQueue);
    const mv::BlockGoalFn goal = [&](const mv::FighterBlock& k, mv::V2& out) {
        if (k.kind != mv::kFighterBlockObject) {
            out = k.point;
            return true;
        }
        const Unit* g = land_goal_unit(st, k.object);
        if (!g) return false;
        const Vec3 p = st.position(g->h);
        out = {p.x, p.y};
        return true;
    };
    const auto host_queued = [this](objects::ObjectId u) {
        const auto it = std::lower_bound(queues_.begin(), queues_.end(), u, [](const OrderQueue& q, objects::ObjectId v) { return q.id < v; });
        return it != queues_.end() && it->id == u;
    };
    for (const objects::ObjectId id : ids) {
        const objects::ObjectId mover = attack_mover(id);
        LandFormationRecord* r = movement_ && movement_->has_unit(mover) ? land_formation_mut(mover) : nullptr;
        const mv::FighterBlock* run = r && r->running_end() > 0 ? &r->blocks[r->running_end() - 1] : nullptr;
        bool hosted = false;
        if (run)
            for (const objects::ObjectId u : land_mover_units(mover)) hosted = hosted || host_queued(u);
        // #490: a guard of an object runs too (its waypoints wait below it until the guarded object stops)
        const bool attack = run && run->kind == mv::kFighterBlockObject && (run->flags & mv::kMoveFlagGuardMove) == 0;
        if (!run || attack || hosted) {
            rest.push_back(id);
            continue;
        }
        if (std::find(done.begin(), done.end(), mover) != done.end()) continue;
        done.push_back(mover);
        (void)mv::stack_append(r->blocks, b, movement_->constants().max_waypoints_per_path, kLandPatrolRadius, goal);
    }
    return rest;
}

// #489 (A1_READINGS §28.9, HERO_CLASH §3.2, as advance_queues' locked units): a movement-locked land mover (hero clash,
// script lock, ability immobilize, landing exit) loses the player waypoints (0x04) below its running block. [port] Retail's
// lock callers leave every coordinator, which discards the formation's queued moves; the record and its running block stay.
void TacticalBattle::land_waypoints_drop_locked() {
    if (space_ || lformations_.empty()) return;
    for (LandFormationRecord& r : lformations_) {
        if (r.blocks.size() < 2) continue;
        bool locked = false;
        for (const objects::ObjectId id : land_mover_units(r.mover)) locked = locked || movement_locked(id);
        if (!locked) continue;
        const size_t run = r.running_end();
        for (size_t i = run > 0 ? run - 1 : 0; i-- > 0;)
            if ((r.blocks[i].flags & mv::kMoveFlagPlayerOrder) != 0) r.blocks.erase(r.blocks.begin() + static_cast<std::ptrdiff_t>(i));
    }
}

// #489 (Arrival 0x423760 [H], FIGHTER_FLYING §4.11 B 2): a land formation whose top is a position block pops once every
// member is done (no movement goal, no arrival turn: the locomotor's stack is empty). n = 1 stays (state 3); a player
// waypoint top (0x04) pops only when blocks[n-2] is queued (0x08), else it stays; any other position top pops. An object
// top never pops on arrival, and the auto block is land_engage_service's. [port] The formation is the records whose
// members share a form_id (0: the record alone) and whose stacks are equal: it waits for all of them, pops them together
// and runs the new top as one ordered move of all their units (exec_move: a new composition, the ordered move's combat
// clears; the stacks are kept). Serial, ascending mover, after land_engage_service.
void TacticalBattle::land_waypoint_service(TacticalMode& mode, SimRng& rng) {
    if (space_ || !movement_ || lformations_.empty()) return;
    struct Group {
        uint32_t form = 0;
        size_t first = 0;
        bool done = true;
        std::vector<objects::ObjectId> movers;
    };
    std::vector<Group> groups;
    for (size_t i = 0; i < lformations_.size(); ++i) {
        const LandFormationRecord& r = lformations_[i];
        const size_t n = r.blocks.size();
        if (n < 2 || r.locked) continue;
        const mv::FighterBlock& t = r.blocks.back();
        if (t.pushed || t.kind == mv::kFighterBlockObject) continue;
        if ((t.flags & mv::kMoveFlagPlayerOrder) != 0 && (r.blocks[n - 2].flags & mv::kMoveFlagQueue) == 0) continue;
        bool done = true, any = false;
        uint32_t form = 0;
        for (const objects::ObjectId u : land_mover_units(r.mover)) {
            if (!movement_->has_unit(u)) continue;
            const mv::UnitView v = movement_->view(u);
            any = true;
            done = done && !v.has_goal && !v.arrival_turn;
            if (form == 0) form = v.form_id;
        }
        if (!any) continue;
        Group* g = nullptr;
        if (form != 0)
            for (Group& c : groups) {
                const LandFormationRecord& f = lformations_[c.first];
                bool same = c.form == form && f.blocks.size() == n;
                for (size_t k = 0; same && k < n; ++k) same = mv::same_block(f.blocks[k], r.blocks[k]);
                if (same) {
                    g = &c;
                    break;
                }
            }
        if (!g) {
            groups.push_back({form, i, true, {}});
            g = &groups.back();
        }
        g->done = g->done && done;
        g->movers.push_back(r.mover);
    }
    for (const Group& g : groups) {
        if (!g.done) continue;
        for (const objects::ObjectId m : g.movers)
            if (LandFormationRecord* r = land_formation_mut(m)) {
                if (const std::optional<mv::FighterBlock> p = mv::stack_pop(r->blocks)) land_pop_effects(*p, m);
                ++stats_.land_waypoint_pops;
            }
        land_run_top(mode, g.movers, rng);
    }
}

// #489 / #490: the records of `movers` plan their new top together (the first record's top): a position block is one ordered move of
// all their unlocked units (exec_move: a new composition, the ordered move's combat clears) with the stacks kept; an object
// block re-plans per record as after a Pop.
void TacticalBattle::land_run_top(TacticalMode& mode, std::span<const objects::ObjectId> movers, SimRng& rng) {
    std::vector<objects::ObjectId> us;
    std::vector<LandFormationRecord> keep;
    for (const objects::ObjectId m : movers) {
        const LandFormationRecord* r = land_formation(m);
        if (!r || r->blocks.empty()) continue;
        keep.push_back(*r);
        for (const objects::ObjectId u : land_mover_units(m))
            if (!movement_locked(u)) us.push_back(u);
    }
    if (keep.empty() || us.empty()) return;
    const mv::FighterBlock t = keep.front().blocks.back();
    if (t.kind == mv::kFighterBlockObject) {
        for (const LandFormationRecord& k : keep)
            if (LandFormationRecord* r = land_formation_mut(k.mover)) land_replan(mode, *r, rng);
        return;
    }
    std::sort(us.begin(), us.end());
    exec_move(mode, us, t.point.x, t.point.y, rng, t.facing, (t.flags & mv::kMoveFlagAttackMove) != 0,
              static_cast<uint8_t>(t.flags & ~(mv::kMoveFlagAttackMove | LandFormationRecord::kStackFlags)), false);
    // exec_move's record write (land_order) is a fresh order's (a new stack); the walk keeps the stacks
    for (const LandFormationRecord& k : keep)
        if (LandFormationRecord* r = land_formation_mut(k.mover)) *r = k;
}

// #490 (0x4192b0 step 3 [H], FIGHTER_FLYING §4.11 A / B 2: "a guard followed by shift waypoints lasts only while G moves"):
// at a 30-frame check of the guard follow of `target`, the records of `movers` whose top is that guard block (kind 3,
// 0x02) with blocks below it drop it once the guarded object is not moving (no patrol re-queue, no goal update, no 0x2a)
// and plan the next block together. [port] As the fighters' fform_recheck, the arrived formation (state 3, 0x410a60) gets
// the same drop: the port's members can finish the plan move to a still object before the check. G moves as the fighters'
// fobject_moving (a movement goal, not arrived; no movement unit: still). Returns the movers whose guard was dropped.
std::vector<objects::ObjectId> TacticalBattle::land_guard_pop(TacticalMode& mode, std::span<const objects::ObjectId> movers,
                                                              objects::ObjectId target, SimRng& rng) {
    std::vector<objects::ObjectId> popped;
    if (space_ || !movement_) return popped;
    const objects::ObjectId gm = attack_mover(target);
    if (movement_->has_unit(gm)) {
        const mv::UnitView g = movement_->view(gm);
        if (g.has_goal && !g.arrived) return popped;  // G moves
    }
    for (const objects::ObjectId m : movers) {
        LandFormationRecord* r = land_formation_mut(m);
        if (!r || r->blocks.size() < 2) continue;
        const mv::FighterBlock& t = r->blocks.back();
        if (t.pushed || t.kind != mv::kFighterBlockObject || (t.flags & mv::kMoveFlagGuardMove) == 0) continue;
        if (t.object != target && attack_mover(t.object) != gm) continue;  // the top guards this follow's object
        r->blocks.pop_back();  // F +0x100 -= 0x68
        ++stats_.land_guard_pops;
        popped.push_back(m);
    }
    if (!popped.empty()) land_run_top(mode, popped, rng);
    return popped;
}

}  // namespace alamo::sim
