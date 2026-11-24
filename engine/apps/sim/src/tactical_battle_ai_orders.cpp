// #147: the tactical AI's order rules (BattleAi before), reached through apply_order with the AI as the issuer, and the
// AI's follow re-paths and guard drops. The rules are the AI's own (Lua Move_To / Attack_Move / Guard_Target, task-force
// group orders, team containers, follow records), not the player path's; moving them here changed none of them.
#include "tactical_battle_internal.hpp"

#include <bit>

#include "alamo/ai/tactical/tactical_ai.hpp"  // kTfAttackMoveIssueAttack
#include "alamo/lua/object_bridge.hpp"        // LuaOrder

namespace alamo::sim {

namespace at = alamo::ai::tactical;

namespace {

uint32_t le32(std::span<const uint8_t> b, size_t o) {
    return static_cast<uint32_t>(b[o]) | (static_cast<uint32_t>(b[o + 1]) << 8) | (static_cast<uint32_t>(b[o + 2]) << 16) |
           (static_cast<uint32_t>(b[o + 3]) << 24);
}

}  // namespace

cmd::RejectReason TacticalBattle::ai_order(TacticalMode& mode, uint8_t player, const cmd::EventData& ev, AiOrderIssuer& ai,
                                           SimRng& rng) {
    using R = cmd::RejectReason;
    using T = cmd::EventType;
    if (finished_) return R::NotAllowed;
    // the ordered-attack hostility test is 0x39a450 -> Is_Enemy 0x2824f0 (relation == 1): the battle's relation table
    // (TACTICAL_COMBAT §2.10), where an Is_Neutral player is relation 2 (combat_->is_enemy)
    objects::ObjectStore& st = mode.primary();
    // battle host: unit orders resolve through team_units (a unit, or a TEAM container's live members).
    switch (ev.type()) {
        case T::MoveToPositionFacing: {
            const tactical::Aabb& bb = mode.bounds();
            mv::MoveOrder o;
            for (const uint32_t id : ev.ids("units"))  // battle host: TEAM containers -> their members
                for (const objects::ObjectId mid : team_units(mode, player, id))
                    if (unit_by_id(mid)->movable && std::find(o.units.begin(), o.units.end(), mid) == o.units.end()) o.units.push_back(mid);
            if (o.units.empty()) return R::InvalidUnit;
            const float x = ev.f("x"), y = ev.f("y");
            if (!std::isfinite(x) || !std::isfinite(y)) return R::InvalidPosition;
            drop_locked(o.units);  // OI follow-up t03: the group move leaves locked units out
            if (o.units.empty()) return R::None;
            o.destination = {std::clamp(x, bb.min.x, bb.max.x), std::clamp(y, bb.min.y, bb.max.y)};
            ai.follow_drop(o.units);
            for (const objects::ObjectId mid : o.units) if (abilities_) abilities_->move_ordered(mid, frame_);  // Terminate_Effect_On_Move_Command (MCX4_T10_RE_ANSWERS §13)
            movement_->move(o, rng, frame_);
            for (const objects::ObjectId id : o.units) {
                combat_->clear_order(id);
                combat_->set_attack_goal(id, tc::kNoObject);  // a position block
            }
            fighter_order(o.units, 0, Vec3{o.destination.x, o.destination.y, 0.0f});  // the squadrons' formation
            return R::None;
        }
        case T::MoveToObject: {
            // Lua Move_To(object) without a threat threshold (ev_move_to_object): a move to a snapshot of the target's
            // position, as the LuaOrder MoveTo path. s78n t26 review round 2 (LUA_API "Move_To / Attack_Move / Guard_Target"
            // [H]; TACTICAL_COMBAT §2.8 "Summary by order"): Move_Internal's flags 0 give a kind-1 position block, which is
            // never followed (A1_READINGS §33.4: the 30-frame re-path follows kind-3 goals only) - no follow record; the
            // destination check and the single-member shortcut apply as for a position.
            const uint32_t tw = ev.u("target");
            const objects::ObjectHandle th = tw <= 0x7FFFFFFFu ? st.find(static_cast<objects::ObjectId>(tw)) : objects::ObjectHandle{};
            if (!st.alive(th)) {
                ai.stale_target();
                return R::None;  // legacy docs/re/COMMANDS.md sec. 6.2: a missing / dying target is a silent no-op
            }
            mv::MoveOrder o;
            for (const uint32_t id : ev.ids("movers"))  // battle host: TEAM containers -> their members
                for (const objects::ObjectId mid : team_units(mode, player, id))
                    if (mid != static_cast<objects::ObjectId>(tw) && unit_by_id(mid)->movable &&
                        std::find(o.units.begin(), o.units.end(), mid) == o.units.end())
                        o.units.push_back(mid);
            if (o.units.empty()) return R::InvalidUnit;
            const Vec3 tp = st.position(th);
            drop_locked(o.units);  // OI follow-up t03
            if (o.units.empty()) return R::None;
            for (const objects::ObjectId id : o.units) combat_->set_attack_goal(id, tc::kNoObject);  // kind 1
            fighter_order(o.units, 0, tp);  // a position snapshot (kind 1)
            ai.follow_drop(o.units);
            for (const objects::ObjectId mid : o.units) if (abilities_) abilities_->move_ordered(mid, frame_);  // Terminate_Effect_On_Move_Command (MCX4_T10_RE_ANSWERS §13)
            if (o.units.size() == 1) {
                combat_->clear_order(o.units.front());
                return ai_move_unit(mode, rng, o.units.front(), tp.x, tp.y) ? R::None : R::InvalidUnit;
            }
            const tactical::Aabb& gb = mode.bounds();
            o.destination = {std::clamp(tp.x, gb.min.x, gb.max.x), std::clamp(tp.y, gb.min.y, gb.max.y)};
            movement_->move(o, rng, frame_);
            for (const objects::ObjectId id : o.units) combat_->clear_order(id);
            return R::None;
        }
        case T::Attack: {
            const uint32_t tw = ev.u("target");
            const TacticalBattle::Unit* t = tw <= 0x7FFFFFFFu ? unit_by_id(static_cast<objects::ObjectId>(tw)) : nullptr;
            // q6: a TEAM container is a valid target on its own (A1_READINGS §33.2.1 row 1, 0x5680b0); each attacker attacks
            // the container's live member nearest it, re-picked until the container is gone (TacticalBattle::TeamAttack).
            const TacticalBattle::Container* ct = t || tw > 0x7FFFFFFFu ? nullptr : team_target(static_cast<objects::ObjectId>(tw));
            if (ct ? !combat_->is_enemy(static_cast<int32_t>(player), st.owner(ct->h)) : (!t || !t->alive || t->retreated || !combat_->is_enemy(static_cast<int32_t>(player), st.owner(t->h))))
                return R::InvalidTarget;
            int32_t hp = -1;
            if (const uint32_t ref2 = ev.u("ref2"); t && ref2 > 0)
                for (uint32_t k = 0; k < combat_->hardpoint_count(t->id) && hp < 0; ++k)
                    if (static_cast<uint32_t>(combat_->hardpoint_kind(t->id, k)) == ref2 - 1 && combat_->hardpoint_targetable(t->id, k))
                        hp = static_cast<int32_t>(k);
            uint32_t n = 0;
            std::vector<objects::ObjectId> attackers;  // battle host: TEAM containers -> their members
            for (const uint32_t id : ev.ids("attackers"))
                for (const objects::ObjectId mid : team_units(mode, player, id))
                    if (std::find(attackers.begin(), attackers.end(), mid) == attackers.end()) attackers.push_back(mid);
            ai.follow_drop(attackers);  // a newer order (signal 0xb, A1_READINGS §33.3)
            std::vector<TacticalBattle::TeamPick> picks;
            // s78n t25.3 (MOVEMENT §4.3.2 item 2): on land the attackers of each resolved target (groups in order of first
            // appearance) form one movement order with the attack block (TacticalBattle::land_attack_order)
            std::vector<std::pair<objects::ObjectId, std::vector<objects::ObjectId>>> groups;
            for (const objects::ObjectId aid : attackers) {
                const TacticalBattle::Unit* u = unit_by_id(aid);
                if (!u) continue;
                const TacticalBattle::Unit* tu = t;
                if (ct) {
                    const objects::ObjectId m = team_pick(mode, *ct, st.position(u->h), combat_->share_group(u->id), picks);
                    tu = m != objects::kNoObjectId ? unit_by_id(m) : nullptr;
                    if (!tu) continue;
                }
                const Vec3 tp = st.position(tu->h);
                // cmb1 t02 (BKR1 Q2 [H]): on land Order_Attack(ordered 1) takes order_attack_issue's paths (a unit that is
                // not a team container / member / LUCKY_SHOT: weapon path, no unlocked ordered target; path B locks the
                // goal in weapon range). [port] a TEAM container target keeps the ordered slot (TeamAttack).
                if (!space_ && !ct) (void)combat_->order_attack_issue(u->id, tu->id, hp);
                else combat_->order_attack(u->id, tu->id, hp);
                combat_->set_attack_goal(u->id, tu->id, hp);  // Attack_Target block (kind 3, hard point, §2.8)
                if (ct && combat_->has_unit(u->id)) team_attack_set(u->id, ct->id, tu->id, true);
                if (!movement_locked(u->id))  // an object block; a locked unit keeps its formation (as Move_To)
                    fighter_order(std::span<const objects::ObjectId>(&u->id, 1), 0, tp,
                                      ct && movement_->fighter_squad(ct->id) ? ct->id : tu->id);
                ++n;
                if (!u->movable) continue;
                if (!space_) {  // s78n t20 stage A / t25.3 (MOVEMENT §4.3.2, FIGHTER_FLYING §4.11 A / E)
                    auto g = std::find_if(groups.begin(), groups.end(), [&](const auto& x) { return x.first == tu->id; });
                    if (g == groups.end()) g = groups.insert(groups.end(), {tu->id, {}});
                    g->second.push_back(u->id);
                    attack_block_set(u->id, ct ? ct->id : tu->id, 0, tp, ct ? -1 : hp);  // the named hard point
                    continue;
                }
                const Vec3 up = st.position(u->h);
                const float dx = tp.x - up.x, dy = tp.y - up.y, d = dmath::sqrt(dx * dx + dy * dy);
                const float range = std::max(u->range, 100.0f);
                // as the human path (TacticalBattle::exec_attack, FIGHTER_FLYING §4.11 B 1 / B 3): a fighter's move
                // names the target as its object block; in range nobody moves and the formation is still replaced.
                // with a TEAM container target `t` is null; the block is the human path's (the squadron
                // container itself, else the picked member).
                const objects::ObjectId block = ct && movement_->fighter_squad(ct->id) ? ct->id : tu->id;
                const objects::ObjectId fobj = ai_fighter(u->id) ? block : objects::kNoObjectId;
                if (d > 0.8f * range) {
                    const float k = (d - 0.7f * range) / d;
                    // s78n t20 fix (MOVEMENT 4.3.1 item 2): the approach stands for the attack's object-goal block -
                    // no destination check (as TacticalBattle::attack_approach)
                    ai_move_unit(mode, rng, u->id, up.x + dx * k, up.y + dy * k, mv::kThreatOff, 0, fobj, true);
                } else if (fobj != objects::kNoObjectId && !movement_locked(u->id)) {
                    mv::MoveOrder o;
                    o.units = {u->id};
                    const tactical::Aabb& gb = mode.bounds();
                    o.destination = {std::clamp(tp.x, gb.min.x, gb.max.x), std::clamp(tp.y, gb.min.y, gb.max.y)};
                    o.goal_object = fobj;
                    movement_->fighter_new_order(o, frame_);
                }
            }
            for (const auto& [tid, us] : groups)
                (void)land_attack_order(mode, us, tid, st.position(unit_by_id(tid)->h), 0, false, rng);
            return n ? R::None : R::InvalidUnit;
        }
        case T::PortExtension: {
            const uint32_t sub = ev.u("subtype");
            const auto b = ev.bytes_view("data");
            if (sub < lua::kLuaOrderSubtype || sub > lua::kLuaOrderSubtype + 0xFFu || b.size() < lua::kLuaOrderPayloadBytes) return R::Unhandled;
            const auto order = static_cast<lua::LuaOrder>(sub - lua::kLuaOrderSubtype);
            // battle host: a TEAM container's order goes to its live members (the container itself has no movement).
            // a task-force group order (tactical_ai.hpp HOST CONTRACT) carries its further units after the 24-byte
            // block, one u32 each: all of them form one order (MOVEMENT §6.3 [H]: one coordinator for the task force).
            std::vector<objects::ObjectId> ids = team_units(mode, player, le32(b, 0));
            for (size_t off = lua::kLuaOrderPayloadBytes; off + 4 <= b.size(); off += 4)
                for (const objects::ObjectId mid : team_units(mode, player, le32(b, off)))
                    if (std::find(ids.begin(), ids.end(), mid) == ids.end()) ids.push_back(mid);
            if (ids.empty()) return R::InvalidUnit;
            if (order == lua::LuaOrder::Stop) {
                ai.follow_drop(ids);  // a1h t02
                for (const objects::ObjectId id : ids) {
                    const objects::ObjectId one[] = {id};
                    if (unit_by_id(id)->movable) movement_->stop(one);
                    combat_->clear_order(id);
                    combat_->set_attack_goal(id, tc::kNoObject);  // refixf t06
                }
                fighter_order_clear(ids);  // Stop leaves the formation [M]
                return R::None;
            }
            if (order == lua::LuaOrder::HyperspaceAway) {
                // Hyperspace_Away of a VM without the hyperspace hook (with it the binding calls
                // TacticalScriptHooks::hyperspace_out_link itself): the battle's departure for each unit, delay 0, keep =
                // the order's argument. Refused when no unit can jump (land, no locomotor, already leaving).
                bool any = false;
                for (const objects::ObjectId id : ids) any = hyperspace_out(id, 0, le32(b, 20) != 0) || any;
                return any ? R::None : R::NotAllowed;
            }
            // ref2 t06 [port] documented refusal: the battle's hyperspace departures have no cancel (Cancel_Hyperspace's
            // locomotor / reinforcement side is not modelled, bind_objects.cpp m_cancel_hyperspace).
            if (order == lua::LuaOrder::CancelHyperspace) return R::NotAllowed;
            if (order != lua::LuaOrder::MoveTo && order != lua::LuaOrder::AttackMove && order != lua::LuaOrder::Guard) return R::Unhandled;
            mv::MoveOrder group;
            for (const objects::ObjectId id : ids)
                if (unit_by_id(id)->movable) group.units.push_back(id);
            if (group.units.empty()) return R::InvalidUnit;
            ai.follow_drop(ids);  // the collector raises signal 0xb on each unit it takes (A1_READINGS §33.3)
            const TacticalBattle::Unit* u = unit_by_id(group.units.front());
            float x = std::bit_cast<float>(le32(b, 8)), y = std::bit_cast<float>(le32(b, 12));
            const uint32_t tw = le32(b, 4);
            bool point_goal = false;  // S7 t29 rework: a land Attack_Move on a non-hostile object goes to its position
            bool nonhostile_block = false;  // a unit Attack_Move on a non-hostile object (land attack block, no lock)
            if (tw != cmd::kNoObject) {
                const objects::ObjectHandle th = tw <= 0x7FFFFFFFu ? st.find(static_cast<objects::ObjectId>(tw)) : objects::ObjectHandle{};
                if (!st.alive(th)) {
                    ai.stale_target();
                    return R::None;  // legacy docs/re/COMMANDS.md sec. 6.2: a missing / dying target is a silent no-op (stale script reference)
                }
                const Vec3 tp = st.position(th);
                x = tp.x;
                y = tp.y;
                const TacticalBattle::Unit* tu = unit_by_id(static_cast<objects::ObjectId>(tw));
                const bool attackable = tu && tu->alive && !tu->retreated && combat_->has_unit(tu->id);
                if (order == lua::LuaOrder::AttackMove && attackable && !combat_->is_enemy(static_cast<int32_t>(player), st.owner(tu->h))) {
                    // S7 t29 rework (TACTICAL_COMBAT §2.10 "Task-force Attack_Move on a non-hostile object"): not refused.
                    // Land (0x6e9440): the goal is the object's position, an attack-move to that point; space (0x6e4fc0): the
                    // object stays the goal. No Order_Attack, no formation attack target (path B needs relation 1): the units
                    // go there, auto-engage on the way and never attack the object itself.
                    for (const objects::ObjectId id : group.units) combat_->set_attack_goal(id, tc::kNoObject);
                    // bkt1 t05 (BKT1_ATTACK_RE §3 [H]): an object's own Attack_Move (arg 0; Move_Internal 0x57da90) is
                    // the land attack move on the object itself - kind 3 with flag 0x01, the exact goal, slots, staging and
                    // the fire trim as for an enemy, no lock (the attack block below, kAttackBlockNoLock)
                    point_goal = !space_ && (le32(b, 20) & at::kTfAttackMoveIssueAttack) != 0;
                    // (not on the caller itself or its container: BKT1_ATTACK_RE §3.1, those stay a move to the point). Case (b):
                    // the caller is the order's object (payload word 0); a squadmate of the caller, or a member of a
                    // calling squad container, is neither, so it gets the block. tu is a battle unit, never a container, so
                    // the caller's container (+0x2b0) cannot be it.
                    // A task force's further units (after byte 24) are not callers: BKT1 §3.1 is the object's own Move_Internal.
                    // (review) A squadmate or own-member target is in group.units: the block's item-6 stop (0x40edd0, every
                    // member can fire from where it stands) ends it at once, so the formation does not chase its own member.
                    const bool self = tu->id == static_cast<objects::ObjectId>(le32(b, 0));
                    nonhostile_block = !space_ && !point_goal && !self;
                    if (self) point_goal = !space_;
                } else if (order == lua::LuaOrder::AttackMove && attackable) {
                    // refixf t06 (TACTICAL_COMBAT §2.8): an Attack_Move on an object. A: a task-force order (arg bit, header)
                    // issues Order_Attack (ordered 1) to every unit now; a refusing unit is dropped from the move, none left
                    // -> the order fails. B: the object is each moving unit's formation attack goal (the combat system locks
                    // it in weapon range). This replaces the a1f t07 neutral-target stand-in (§2.8: an enemy Is_Neutral
                    // player's object qualifies for B). An object's own Lua Attack_Move (arg 0) gets B only.
                    if (le32(b, 20) & at::kTfAttackMoveIssueAttack) {
                        std::vector<objects::ObjectId> kept;
                        for (const objects::ObjectId id : group.units)
                            if (!combat_->has_unit(id) || combat_->order_attack_issue(id, tu->id, -1)) kept.push_back(id);
                            else ai.order_refused(id);  // not moved, not pending
                        for (const objects::ObjectId id : ids)  // members that do not move still get the order
                            if (std::find(group.units.begin(), group.units.end(), id) == group.units.end() && combat_->has_unit(id))
                                combat_->order_attack_issue(id, tu->id, -1);
                        if (kept.empty()) return R::InvalidTarget;
                        group.units = std::move(kept);
                        u = unit_by_id(group.units.front());
                    }
                    for (const objects::ObjectId id : group.units) combat_->set_attack_goal(id, tu->id, -1);
                    // Staging (0x40e260 [M], §2.8): land formations (type 1) only - a single space Attack_Move is not staged.
                    // on land it is TacticalBattle::attack_stage_goal, inside the attack order below.
                } else if (order == lua::LuaOrder::AttackMove) {
                    for (const objects::ObjectId id : group.units) combat_->set_attack_goal(id, tc::kNoObject);
                    // cmb2 t06 (BKT1_ATTACK_RE §3.1 / §3.5 [H]): an object's own Attack_Move (arg 0) on an object that is
                    // not a combat unit (a story marker) is the land attack move too - kind 3 with flag 0x01 on the object, no
                    // lock (TARGETING by geometry: TacticalBattle::attack_can_hit_at); not on the caller or its container
                    // (review: not a TEAM container - its members are the combat units - and not a retreated unit)
                    const auto obj = static_cast<objects::ObjectId>(tw);
                    // cmb3 t01 (b) review: the same caller-only rule as the combat-unit branch (payload word 0 or its container)
                    const auto caller = static_cast<objects::ObjectId>(le32(b, 0));
                    const bool self = obj == caller || attack_mover(caller) == obj;
                    const bool combat_object = (tu && (tu->retreated || !tu->alive || combat_->has_unit(tu->id))) ||
                                               team_target(obj) != nullptr || movement_->is_land_team_container(obj);
                    nonhostile_block = !space_ && !self && !combat_object && (le32(b, 20) & at::kTfAttackMoveIssueAttack) == 0;
                }
            }
            if (order != lua::LuaOrder::AttackMove || tw == cmd::kNoObject)  // position / guard / move blocks
                for (const objects::ObjectId id : group.units) combat_->set_attack_goal(id, tc::kNoObject);
            if (!std::isfinite(x) || !std::isfinite(y)) return R::InvalidPosition;
            // the squadrons' formation - block flags 0x01 attack-move / 0x02 guard; an Attack_Move / Guard of an
            // object keeps it as the slot goal (Move_To(object) is a position snapshot, kind 1).
            const uint8_t fflags = order == lua::LuaOrder::AttackMove ? mv::kMoveFlagAttackMove
                                   : order == lua::LuaOrder::Guard    ? mv::kMoveFlagGuardMove
                                                                      : uint8_t{0};
            const objects::ObjectId fobj = tw != cmd::kNoObject && order != lua::LuaOrder::MoveTo && !point_goal
                                               ? static_cast<objects::ObjectId>(tw)
                                               : objects::kNoObjectId;
            const uint32_t targ = le32(b, 20);  // REMAINING_GAPS §4.2: Move_To threat threshold (float bits, 0 = off)
            const float tt = order == lua::LuaOrder::MoveTo && targ != 0u ? std::bit_cast<float>(targ) : mv::kThreatOff;
            // s78n t20 fix (MOVEMENT 4.3.1 item 2; LUA_API "Move_To / Attack_Move / Guard_Target"): Attack_Move and
            // Guard_Target on an object build a kind-3 block, which never takes the position destination check;
            // Move_To(object) is a kind-1 position snapshot and takes it (the same split as `fobj` above)
            const bool object_block = fobj != objects::kNoObjectId;
            // a land Guard_Target on an object is a guard block of the movement (land_attack_order, guard)
            const bool land_guard = !space_ && order == lua::LuaOrder::Guard && object_block;
            // s78n t20 stage A (FIGHTER_FLYING §4.11 A / E, MOVEMENT §4.3.2 item 6): a land Attack_Move on an attackable
            // object (a non-hostile one too, no lock) is an attack block of each mover (TacticalBattle::AttackBlock, whose 30-frame checks re-path it: the
            // object follow below leaves it out), and a unit that can already hit it from where it stands takes no path.
            const TacticalBattle::Unit* atu = tw != cmd::kNoObject && tw <= 0x7FFFFFFFu ? unit_by_id(static_cast<objects::ObjectId>(tw)) : nullptr;
            const bool combat_target = atu && atu->alive && !atu->retreated && combat_->has_unit(atu->id);
            // a non-combat object (nonhostile_block set above) is a no-lock attack block as well
            const bool attack_block = !space_ && order == lua::LuaOrder::AttackMove && !point_goal && tw != cmd::kNoObject &&
                                      (combat_target ? (nonhostile_block || combat_->is_enemy(static_cast<int32_t>(player), st.owner(atu->h))) : nonhostile_block);
            if (attack_block) {
                // s78n t25.3 (MOVEMENT §4.3.2 items 2 / 6 / 7): one movement order with the attack block (flags 0x01) for
                // the movers; the staging 0x40e260 is the builder's (attack_stage_goal); a formation whose members can all
                // fire already is stopped by the plan (item 6), the others take the slot and the fire trim
                const auto aid = static_cast<objects::ObjectId>(tw);
                const Vec3 tp = st.position(st.find(aid));
                const uint8_t bf = static_cast<uint8_t>(mv::kMoveFlagAttackMove | (nonhostile_block ? TacticalBattle::kAttackBlockNoLock : 0));
                for (const objects::ObjectId id : group.units) attack_block_set(id, aid, bf, tp);
                land_order(group.units, mv::kMoveFlagAttackMove, tp, aid);  // the formation's block
                (void)land_attack_order(mode, group.units, aid, tp, mv::kMoveFlagAttackMove, false, rng);
                return R::None;
            }
            // an object goal (kind 3) is followed by the movement layer (§33.4). s78n t26 review round 2: Move_To on an
            // object is a kind-1 position snapshot (LUA_API Move_Internal flags 0 [H]): no follow
            if (tw != cmd::kNoObject && !point_goal && !attack_block && order != lua::LuaOrder::MoveTo) {
                const Vec3 tp = st.position(st.find(static_cast<objects::ObjectId>(tw)));
                // fighters given an object block follow it in their own formation (0x4192b0 steps 3 / 4,
                // FIGHTER_FLYING §4.11); the follow's re-issued plain move would replace that block with a flags-0 position
                // block (and drop its pushed auto blocks), so they stay out of the follow.
                std::vector<objects::ObjectId> fu;
                for (const objects::ObjectId id : group.units)
                    if (fobj == objects::kNoObjectId || !ai_fighter(id)) fu.push_back(id);
                ai.follow_add(fu, static_cast<objects::ObjectId>(tw), tp.x, tp.y, tt, land_guard);
            }
            if (land_guard) {
                // s78n t25.4 (MOVEMENT §4.3.2 items 1 / 2 / 6 / 7): a land Guard_Target on an object is one movement order
                // with the guard block (flags 0x02): the guard L rule gives the formations farther than min(range, 150)
                // a slot, the guard trim stops them within Land_Guard_Range of the object with a clear line of fire.
                // The follow record above re-plans it (follow_service). Locked units are dropped by the builder.
                for (const objects::ObjectId id : group.units) combat_->clear_order(id);
                const Vec3 gp = st.position(st.find(fobj));
                land_order(group.units, fflags, gp, fobj);  // the formation's guard block
                // cmb2 t03 (review): a failed first plan waits 2 x FPS before the follow tries again
                if (land_attack_order(mode, group.units, fobj, gp, fflags, false, rng, true) == TacticalBattle::AttackPlan::Failed)
                    ai.follow_retry_at(fobj, frame_ + 61);
                return R::None;
            }
            // refixf t02 / t06 (MOVEMENT §6.3 [H]): Lua Attack_Move / Move_To on a task force build one parameter block and ONE
            // coordinator for all its units, so the formation slots and the destination reservations (§2.4 step 4) give each
            // unit its own spot. The tactical AI now sends that as one group order (the ids above); the t02 stand-in that
            // merged one frame's per-unit orders here is gone. An object's own Lua order stays a one-unit move.
            // mcx4 (MCX4_T10_RE_ANSWERS §13): a Lua Move_To / Attack_Move is a move order (0x4f6b60): Terminate_Effect_On_Move_Command
            for (const objects::ObjectId mid : group.units)
                if (abilities_) abilities_->move_ordered(mid, frame_);
            if (group.units.size() == 1) {  // a single unit: unchanged path
                if (order != lua::LuaOrder::AttackMove) combat_->clear_order(u->id);
                if (!movement_locked(u->id)) fighter_order(group.units, fflags, Vec3{x, y, 0.0f}, fobj);
                return ai_move_unit(mode, rng, u->id, x, y, tt, fflags, fobj, object_block) ? R::None : R::InvalidUnit;  // the same block
            }
            group.threat_threshold = tt;
            group.flags = fflags;  // the movement block = the host record's (flags, object)
            group.goal_object = fobj == objects::kNoObjectId ? -1 : fobj;
            group.skip_destination_check = object_block;  // s78n t20 fix (MOVEMENT 4.3.1 item 2)
            const tactical::Aabb& gb = mode.bounds();
            group.destination = {std::clamp(x, gb.min.x, gb.max.x), std::clamp(y, gb.min.y, gb.max.y)};
            drop_locked(group.units);  // OI follow-up t03: locked members keep their order [M: group-move reading]
            if (group.units.empty()) return R::None;
            movement_->move(group, rng, frame_);  // one formation move of the team
            fighter_order(group.units, fflags, Vec3{group.destination.x, group.destination.y, 0.0f}, fobj);  // S7 t22
            if (order != lua::LuaOrder::AttackMove)
                for (const objects::ObjectId id : group.units) combat_->clear_order(id);
            return R::None;
        }
        case T::Reinforce: {
            // --- A2 tactical: the human reinforcement path (TacticalBattle::order_reinforce) + the tactical
            // population cap (population_cap: the cap the battle itself does not test on land). ---
            const auto key = static_cast<objects::ObjectId>(ev.u("unit"));
            if (!space_) {
                const float cap = ai.population_cap(player);
                for (const TacticalBattle::Participant& p : parts_)
                    if (p.galactic_id == key && p.owner == static_cast<int32_t>(player) && p.reserve && !p.deployed && cap >= 0.0f &&
                        ai.population_used(player) + p.pop > cap)
                        return R::QueueFull;
            }
            return order_reinforce(mode, player, key, ev.f("x"), ev.f("y"), rng);
            // --- end A2 tactical ---
        }
        case T::Retreat: return request_retreat(player) ? R::None : R::NotAllowed;
        case T::Production:  // skirmish station production (Produce_Force of non-UC types): the battle's Production handler
            return hg_production_command(mode, player, ev, true);  // b0147: not charged (the plan's escrow pays)
        case T::TacticalBuild: {
            // --- A2 tactical: TacticalBuildEvent 0x42edf0 for an AI player (L1_S4_REMAINING §3.6): refusals in order,
            // then the build starts at once (pad_build, check_rules false). ---
            // battle host: the shared TacticalBuild hook (TacticalBattle::ai_tactical_build: tactical_build with ai = true,
            // same refusal order, no charge in any mode - b0147: the plan paid at accept into its escrow -, and the base
            // price back when the build left no UC object; the UC / constructed objects become battle combat units).
            const uint32_t pw = ev.u("pad");
            if (pw == commands::kNoObject || pw > 0x7FFFFFFFu) return R::InvalidTarget;
            return ai_tactical_build(mode, player, static_cast<objects::ObjectId>(pw), static_cast<objects::TypeIndex>(ev.u("type_index")),
                                         rng);
            // --- end A2 tactical ---
        }
        // every other event goes through the battle's validate-and-apply entry, as a human's would (Turn_To_Face
        // -> order_facing, the Garrison family -> s4_command, Sell -> tactical_sell, the ability events -> handle_command);
        // no AI order is dropped as Unhandled for want of a branch here. The AI issues no selection-based event.
        default: return apply_order(mode, player, ev, {}, rng);
    }
}

bool TacticalBattle::ai_move_unit(TacticalMode& mode, SimRng& rng, objects::ObjectId id, float x, float y, float threat_threshold,
                                  uint8_t flags, objects::ObjectId goal_object, bool object_block) {
    const TacticalBattle::Unit* u = unit_by_id(id);
    if (!u || !u->alive || u->retreated || !u->movable) return false;
    if (movement_locked(id)) return true;  // OI follow-up t03: the locked unit keeps its order (A1_READINGS §28.9)
    const tactical::Aabb& bb = mode.bounds();
    mv::MoveOrder o;
    o.units = {id};
    o.destination = {std::clamp(x, bb.min.x, bb.max.x), std::clamp(y, bb.min.y, bb.max.y)};
    o.threat_threshold = threat_threshold;
    o.flags = flags;  // the order's block flags (0x01 attack-move / 0x02 guard)
    o.goal_object = goal_object == objects::kNoObjectId ? -1 : goal_object;  // the object block (movement: -1 = none)
    // s78n t20 (MOVEMENT 4.3.1 items 2 / 6): an object goal (block kind 3) takes neither the destination check nor the
    // single-member shortcut. Land attack blocks are TacticalBattle::land_attack_order; what is left here on
    // land is the a1h follow and the Attack_Move blocks on a non-attackable object ([port]; Guard_Target on an
    // object is a guard block of land_attack_order). The check's object test has no exemption for the order's target (retail sends kind 3 to
    // the attack-arc slot test 0x438c10, which leaves the target out), so run on an object goal it moves the goal off
    // the very object the units were sent to: attackers end out of weapon range of a standing enemy and never fire.
    o.skip_destination_check = object_block || goal_object != objects::kNoObjectId;
    movement_->move(o, rng, frame_);
    return true;
}

bool TacticalBattle::ai_fighter(objects::ObjectId id) const {
    return space_ && movement_->has_unit(id) && movement_->view(id).locomotor == mv::LocomotorKind::Fighter;
}

std::vector<objects::ObjectId> TacticalBattle::team_units(const TacticalMode& mode, uint8_t player, uint32_t wire) {
    std::vector<objects::ObjectId> out;
    if (wire == cmd::kNoObject || wire > 0x7FFFFFFFu) return out;
    const auto id = static_cast<objects::ObjectId>(wire);
    const objects::ObjectStore& st = mode.primary();
    if (const TacticalBattle::Unit* u = unit_by_id(id)) {
        if (u->alive && !u->retreated && st.owner(u->h) == static_cast<int32_t>(player)) out.push_back(u->id);
        return out;
    }
    for (size_t c = 0; c < containers_.size(); ++c) {  // a team container: its live members, ascending id
        if (containers_[c].id != id || containers_[c].removed) continue;
        for (const TacticalBattle::Unit& m : units_)
            if (m.container == c && m.alive && !m.retreated && st.owner(m.h) == static_cast<int32_t>(player)) out.push_back(m.id);
        break;
    }
    return out;
}

void TacticalBattle::ai_follow_move(TacticalMode& mode, const std::vector<objects::ObjectId>& units, Vec3 tp, float threat, SimRng& rng) {
    // s78n t20 fix (MOVEMENT 4.3.1 item 2): the follow stands for the formation's re-path of a kind-3 goal
    // (0x4192b0) - no destination check. (s78n t26 review round 2: Move_To(object) adds no follow, it is kind 1.)
    if (units.size() == 1) {
        ai_move_unit(mode, rng, units.front(), tp.x, tp.y, threat, 0, objects::kNoObjectId, true);
        return;
    }
    const tactical::Aabb& bb = mode.bounds();
    mv::MoveOrder o;
    o.units = units;
    o.threat_threshold = threat;
    o.destination = {std::clamp(tp.x, bb.min.x, bb.max.x), std::clamp(tp.y, bb.min.y, bb.max.y)};
    o.skip_destination_check = true;
    movement_->move(o, rng, frame_);
}

void TacticalBattle::ai_guard_drop(TacticalMode& mode, const std::vector<objects::ObjectId>& movers, mv::V2 at_point, SimRng& rng) {
    // a position guard at L (flags 0x02 kept: the record's goal L, no object; every unit's attack order and attack goal
    // cleared, the movement's attack block dropped, the plan to L)
    std::vector<objects::ObjectId> us;
    for (const objects::ObjectId m : movers)
        for (const objects::ObjectId u : land_mover_units(m)) {
            us.push_back(u);
            if (combat_->has_unit(u)) {
                combat_->clear_order(u);
                combat_->set_attack_goal(u, tactical::combat::kNoObject);
            }
        }
    movement_->clear_attack_block(movers);
    land_order(us, mv::kMoveFlagGuardMove, Vec3{at_point.x, at_point.y, 0.0f});
    mv::MoveOrder o;
    for (const objects::ObjectId u : us)
        if (!movement_locked(u)) o.units.push_back(u);
    if (!o.units.empty()) {
        const tactical::Aabb& bb = mode.bounds();
        o.destination = {std::clamp(at_point.x, bb.min.x, bb.max.x), std::clamp(at_point.y, bb.min.y, bb.max.y)};
        o.flags = mv::kMoveFlagGuardMove;
        movement_->move(o, rng, frame_);
    }
}

}  // namespace alamo::sim
