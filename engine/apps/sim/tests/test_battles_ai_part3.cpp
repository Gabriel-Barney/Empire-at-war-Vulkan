// test_battles_ai: part 3 - cmb1 (land Auto_Engage, land scan range, land Order_Attack issue semantics; BKR1_RE_ANSWERS Q2 /
// Q8 [H], FIGHTER_FLYING §4.10 A / §4.11 B / E [H]).
#include "test_battles_ai_common.hpp"

#include <cstring>

namespace bo3split {
namespace {

// The chase ranges on the land types (the test data has none): Idle_Chase_Range 300, Attack_Move_Response_Range 200,
// Guard_Chase_Range 400 on the company (a squad container's ranges, 0x3973b0) and the trooper (a unit's own).
// with g_trooper_hp the trooper also gets the targetable hard point HP_S10_Laser_A
bool g_trooper_hp = false;

std::string chase_ranges(std::string t) {
    for (const char* at : {"<GroundCompany Name=\"S10_Squad\">", "<GroundInfantry Name=\"S10_Trooper\">"})
        if (const size_t p = t.find(at); p != std::string::npos)
            t.insert(p + std::strlen(at), "<Idle_Chase_Range>300</Idle_Chase_Range><Attack_Move_Response_Range>200</Attack_Move_Response_Range>"
                                          "<Guard_Chase_Range>400</Guard_Chase_Range>");
    if (g_trooper_hp)
        if (const size_t p = t.find("<GroundInfantry Name=\"S10_Trooper\">"); p != std::string::npos)
            t.insert(p + std::strlen("<GroundInfantry Name=\"S10_Trooper\">"), "<HardPoints>HP_S10_Laser_A</HardPoints>");
    return t;
}

std::unique_ptr<Data> chase_data(bool squad, bool trooper_hp = false) {
    static std::string prefix;
    const char* const old = g_temp_prefix;
    prefix = std::string(old) + (squad ? "_chase_squad" : "_chase_unit") + (trooper_hp ? "_hp" : "");
    g_temp_prefix = prefix.c_str();
    g_trooper_hp = trooper_hp;
    g_types_patch = chase_ranges;
    auto d = make_data(false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false,
                       false, false, squad);
    g_types_patch = nullptr;
    g_trooper_hp = false;
    g_temp_prefix = old;
    return d;
}

const LandFormationRecord* record_of(const TacticalBattle& b, objects::ObjectId mover) {
    for (const LandFormationRecord& r : b.land_formations())
        if (r.mover == mover) return &r;
    return nullptr;
}

float dist2d(mv::V2 a, Vec3 b) { return std::sqrt((a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y)); }

}  // namespace

// cmb1 t01 / t03 (BKR1 Q2 / Q8 [H]): a land squad (TEAM container, TeamTargeting on the container) and an enemy squad 450
// away (beyond the weapon range 250, inside 250 + Idle_Chase_Range 300).
//   (i) never ordered: no formation record -> scan R, no Auto_Engage: no target, no push for 120 frames;
//   (ii) a short move order: the record (position block) once arrived answers 0x4160a0 = 1 (an arrived single position
//        block) -> the scan cube R + Rr finds the enemy and LocomotorCommon 0x42d0f0 pushes the auto attack block: the
//        record's `pushed` names an enemy member, the squad moves toward it;
//   (iii) the enemy squad is moved 2500 away: the auto block's 30-frame leash (0x4165f0: the object farther than Rr + R from
//        the slot goal) fails -> 0x4105a0 pops it and the position block resumes (the squad's goal is its order point again).
struct LandAutoEngageRun {
    bool found = false, idle_none = false, ordered = false, arrived = false, pushed = false, moved_toward = false;
    bool popped = false, resumed = false;
    uint32_t push_frames = 0, pop_frames = 0;
    int32_t push_hp = -2;  // the hard point the auto block names (-1 none)
};

LandAutoEngageRun run_land_auto_engage(const Data& d) {
    LandAutoEngageRun out;
    CampaignBattleOptions opt;
    opt.conflict.human_choice = galactic::BattleChoice::Fight;
    opt.battle.max_frames = 6000;
    opt.battle.skip_human_units = true;
    opt.battle.order_interval = 0;  // no stand-in orders
    opt.battle.fog = false;
    ClashSession s(d, 0, opt, {"S10_Squad"}, {}, true);
    if (!s.gs) return out;
    int stage = 0;
    uint32_t stage_frame = 0;
    objects::ObjectId sq = objects::kNoObjectId, ec = objects::kNoObjectId;
    std::vector<objects::ObjectId> mem, emem;
    Vec3 dir{}, dest{};
    for (uint64_t t = 0; t < 6000 && stage < 9; ++t) {
        TacticalBattle* b = s.tick();
        TacticalMode* tm = s.host->tactical();
        if (!b || !tm || b->frame() < 3) {
            if (stage > 0) break;
            continue;
        }
        objects::ObjectStore& st = tm->primary();
        auto& cs = const_cast<tc::CombatSystem&>(b->combat());
        const mv::MovementSystem& ms = b->movement();
        if (stage == 0) {
            std::map<uint32_t, std::vector<objects::ObjectId>> teams;
            for (size_t i = 0; i < cs.unit_count(); ++i) {
                const auto id = cs.unit_id_at(i);
                cs.make_invulnerable(id, 1000000, b->frame());
                if (cs.team(id) != 0 && cs.alive(id)) teams[cs.team(id)].push_back(id);
            }
            std::vector<objects::ObjectId> enemies;
            for (const auto& [tid, m] : teams) {
                const auto cid = static_cast<objects::ObjectId>(tid);
                const objects::ObjectHandle th = st.find(cid);
                if (m.size() < 2 || !st.alive(th) || !ms.has_unit(cid)) continue;
                if (st.owner(th) == 0 && sq == objects::kNoObjectId) {
                    sq = cid;
                    mem = m;
                } else if (st.owner(th) == 1) {
                    enemies.push_back(cid);
                }
            }
            out.found = sq != objects::kNoObjectId && !enemies.empty();
            CHECK(out.found);
            if (!out.found) break;
            ec = enemies.front();
            emem = teams[static_cast<uint32_t>(ec)];
            const Vec3 tp = st.position(st.find(ec)), ap = st.position(st.find(sq));
            const float dx = ap.x - tp.x, dy = ap.y - tp.y, dl = std::sqrt(dx * dx + dy * dy);
            dir = dl > 1.0f ? Vec3{dx / dl, dy / dl, 0.0f} : Vec3{1.0f, 0.0f, 0.0f};
            // the other enemy squads far away behind the first one (only `ec` is near)
            for (size_t k = 1; k < enemies.size(); ++k) {
                const objects::ObjectHandle h = st.find(enemies[k]);
                (void)b->teleport(*tm, enemies[k], Vec3{tp.x - dir.x * (2500.0f + 300.0f * static_cast<float>(k)), tp.y - dir.y * 2500.0f, tp.z},
                                  st.facing(h));
            }
            CHECK(b->teleport(*tm, sq, Vec3{tp.x + dir.x * 450.0f, tp.y + dir.y * 450.0f, ap.z}, st.facing(st.find(sq))));
            stage = 1;
            stage_frame = b->frame();
            continue;
        }
        const mv::UnitView v = ms.view(sq);
        if (stage == 1) {  // (i) never ordered
            if (b->frame() < stage_frame + 120) continue;
            // (a squad's TeamTargeting target: read through a member, CombatSystem::current_target)
            out.idle_none = record_of(*b, sq) == nullptr && cs.current_target(mem.front()) == tc::kNoObject && b->stats().land_auto_pushes == 0;
            dest = Vec3{v.pos.x - dir.y * 30.0f, v.pos.y + dir.x * 30.0f, 0.0f};
            out.ordered = b->order_move(*tm, 0, mem, dest.x, dest.y, s.host->random().synced()) == commands::RejectReason::None;
            CHECK(out.ordered && record_of(*b, sq) != nullptr);
            stage = 2;
            stage_frame = b->frame();
            continue;
        }
        if (stage == 2) {  // (ii) arrival, then the push
            const LandFormationRecord* r = record_of(*b, sq);
            if (!out.arrived) {
                out.arrived = !v.has_goal && b->frame() > stage_frame + 5;
                if (out.arrived) stage_frame = b->frame();
                else if (b->frame() > stage_frame + 1500) stage = 9;
                continue;
            }
            if (r && r->pushed() != objects::kNoObjectId) {
                out.pushed = std::find(emem.begin(), emem.end(), r->pushed()) != emem.end();
                out.push_frames = b->frame() - stage_frame;
                out.push_hp = b->attack_block_hp(sq);
                stage = 3;
                stage_frame = b->frame();
            } else if (b->frame() > stage_frame + 300) {
                stage = 9;
            }
            continue;
        }
        if (stage == 3) {  // the squad walks toward the enemy; then the enemy leaves
            if (b->frame() < stage_frame + 20) continue;
            const Vec3 ep = st.position(st.find(ec));
            out.moved_toward = v.has_goal && dist2d(v.goal, ep) < dist2d(mv::V2{dest.x, dest.y}, ep);
            const objects::ObjectHandle eh = st.find(ec);
            CHECK(b->teleport(*tm, ec, Vec3{ep.x - dir.x * 2500.0f, ep.y - dir.y * 2500.0f, ep.z}, st.facing(eh)));
            stage = 4;
            stage_frame = b->frame();
            continue;
        }
        if (stage == 4) {  // (iii) the leash pops the auto block, the order's position block resumes
            const LandFormationRecord* r = record_of(*b, sq);
            if (r && r->pushed() == objects::kNoObjectId && b->stats().land_auto_pops > 0) {
                out.popped = true;
                out.pop_frames = b->frame() - stage_frame;
                out.resumed = v.has_goal && dist2d(v.goal, dest) < 40.0f;
                stage = 9;
            } else if (b->frame() > stage_frame + 200) {
                stage = 9;
            }
        }
    }
    return out;
}

// cmb2 t07 (BKR2_RE_ANSWERS Q3 [H]): a MovingVsStopped step-10 pair {mover, hostile target} pushes the kind-3 attack block
// onto the mover's land formation (TacticalBattle::land_step10_attack, the consumer of MovementSystem::formation_attacks()):
// a squad never ordered has no formation record (F null -> nothing); once ordered, the pair on an enemy member pushes it
// (the record's `pushed`, an attack block, the squad's attack goal); the same pair again and a pair on a friendly unit do
// nothing.
struct LandStep10Run {
    bool found = false, no_record_refused = false, pushed = false, block = false, goal = false, again_refused = false;
    bool friend_refused = false, saved = false;
};

LandStep10Run run_land_step10(const Data& d) {
    LandStep10Run out;
    CampaignBattleOptions opt;
    opt.conflict.human_choice = galactic::BattleChoice::Fight;
    opt.battle.max_frames = 600;
    opt.battle.skip_human_units = true;
    opt.battle.order_interval = 0;
    opt.battle.fog = false;
    ClashSession s(d, 0, opt, {"S10_Squad"}, {}, true);
    if (!s.gs) return out;
    for (uint64_t t = 0; t < 600; ++t) {
        TacticalBattle* b = s.tick();
        TacticalMode* tm = s.host->tactical();
        if (!b || !tm || b->frame() < 3) continue;
        objects::ObjectStore& st = tm->primary();
        auto& cs = const_cast<tc::CombatSystem&>(b->combat());
        const mv::MovementSystem& ms = b->movement();
        objects::ObjectId sq = objects::kNoObjectId, enemy = objects::kNoObjectId;
        std::vector<objects::ObjectId> mem;
        std::map<uint32_t, std::vector<objects::ObjectId>> teams;
        for (size_t i = 0; i < cs.unit_count(); ++i) {
            const auto id = cs.unit_id_at(i);
            if (cs.team(id) != 0 && cs.alive(id)) teams[cs.team(id)].push_back(id);
        }
        for (const auto& [tid, m] : teams) {
            const auto cid = static_cast<objects::ObjectId>(tid);
            const objects::ObjectHandle th = st.find(cid);
            if (m.size() < 2 || !st.alive(th) || !ms.has_unit(cid)) continue;
            if (st.owner(th) == 0 && sq == objects::kNoObjectId) {
                sq = cid;
                mem = m;
            } else if (st.owner(th) == 1 && enemy == objects::kNoObjectId) {
                enemy = m.front();
            }
        }
        out.found = sq != objects::kNoObjectId && enemy != objects::kNoObjectId;
        CHECK(out.found);
        if (!out.found) break;
        SimRng& rng = s.host->random().synced();
        out.no_record_refused = !b->land_step10_attack(*tm, sq, enemy, rng) && record_of(*b, sq) == nullptr;
        const mv::UnitView v = ms.view(sq);
        CHECK(b->order_move(*tm, 0, mem, v.pos.x + 60.0f, v.pos.y, rng) == commands::RejectReason::None);
        out.pushed = b->land_step10_attack(*tm, sq, enemy, rng);
        const LandFormationRecord* r = record_of(*b, sq);
        out.pushed = out.pushed && r && r->pushed() == enemy && b->stats().land_step10_pushes == 1;
        out.block = b->attack_blocks() > 0;
        out.goal = cs.attack_goal(mem.front()) == enemy;
        out.again_refused = !b->land_step10_attack(*tm, sq, enemy, rng);
        out.friend_refused = !b->land_step10_attack(*tm, sq, mem.back(), rng);
        // #488: the record's two-block stack (the order's block, the pushed auto block) survives a battle state round trip
        const LandFormationRecord* r0 = record_of(*b, sq);
        const std::vector<mv::FighterBlock> before = r0 ? r0->blocks : std::vector<mv::FighterBlock>{};
        StateHasher h0;
        b->hash(h0);
        ByteWriter w;
        b->write_part(TacticalBattle::SavePart::State, w);
        ByteReader rd(w.bytes());
        const bool read = b->read_part(TacticalBattle::SavePart::State, rd);
        StateHasher h1;
        b->hash(h1);
        const LandFormationRecord* r1 = record_of(*b, sq);
        bool same = read && r1 && before.size() == 2 && r1->blocks.size() == before.size() && h0.digest() == h1.digest();
        for (size_t i = 0; same && i < before.size(); ++i) {
            const mv::FighterBlock &x = before[i], &y = r1->blocks[i];
            same = mv::same_block(x, y) && x.threat == y.threat && x.point.x == y.point.x && x.point.y == y.point.y;
        }
        out.saved = same;
        break;
    }
    return out;
}

// cmb1 t03 (BKR1 Q8 table [H]): the scan cube of a non-turret land unit moving on a plain Move_To is 0 - a squad that starts
// its move 200 from an enemy squad (inside the weapon range 250) acquires nothing while it walks away; the same start with
// an Attack_Move (R + Attack_Move_Response_Range while under way) acquires the enemy.
struct LandScanMovingRun {
    bool found = false, ordered = false;
    bool acquired = false;
    uint32_t scans = 0;
};

LandScanMovingRun run_land_scan_moving(const Data& d, bool attack_move) {
    LandScanMovingRun out;
    CampaignBattleOptions opt;
    opt.conflict.human_choice = galactic::BattleChoice::Fight;
    opt.battle.max_frames = 3000;
    opt.battle.skip_human_units = true;
    opt.battle.order_interval = 0;
    opt.battle.fog = false;
    ClashSession s(d, 0, opt, {"S10_Squad"}, {}, true);
    if (!s.gs) return out;
    int stage = 0;
    uint32_t stage_frame = 0;
    objects::ObjectId sq = objects::kNoObjectId, ec = objects::kNoObjectId;
    std::vector<objects::ObjectId> mem;
    for (uint64_t t = 0; t < 3000 && stage < 9; ++t) {
        TacticalBattle* b = s.tick();
        TacticalMode* tm = s.host->tactical();
        if (!b || !tm || b->frame() < 3) {
            if (stage > 0) break;
            continue;
        }
        objects::ObjectStore& st = tm->primary();
        auto& cs = const_cast<tc::CombatSystem&>(b->combat());
        const mv::MovementSystem& ms = b->movement();
        if (stage == 0) {
            std::map<uint32_t, std::vector<objects::ObjectId>> teams;
            for (size_t i = 0; i < cs.unit_count(); ++i) {
                const auto id = cs.unit_id_at(i);
                cs.make_invulnerable(id, 1000000, b->frame());
                if (cs.team(id) != 0 && cs.alive(id)) teams[cs.team(id)].push_back(id);
            }
            for (const auto& [tid, m] : teams) {
                const auto cid = static_cast<objects::ObjectId>(tid);
                const objects::ObjectHandle th = st.find(cid);
                if (m.size() < 2 || !st.alive(th) || !ms.has_unit(cid)) continue;
                if (st.owner(th) == 0 && sq == objects::kNoObjectId) {
                    sq = cid;
                    mem = m;
                } else if (st.owner(th) == 1 && ec == objects::kNoObjectId) {
                    ec = cid;
                }
            }
            out.found = sq != objects::kNoObjectId && ec != objects::kNoObjectId && cs.current_target(mem.front()) == tc::kNoObject;
            CHECK(out.found);
            if (!out.found) break;
            const Vec3 tp = st.position(st.find(ec)), ap = st.position(st.find(sq));
            const float dx = ap.x - tp.x, dy = ap.y - tp.y, dl = std::sqrt(dx * dx + dy * dy);
            const Vec3 dir = dl > 1.0f ? Vec3{dx / dl, dy / dl, 0.0f} : Vec3{1.0f, 0.0f, 0.0f};
            CHECK(b->teleport(*tm, sq, Vec3{tp.x + dir.x * 200.0f, tp.y + dir.y * 200.0f, ap.z}, st.facing(st.find(sq))));
            // away from the enemy, slowly enough (speed 1) that it stays inside 250 for the first 45 frames
            out.ordered = b->order_move(*tm, 0, mem, tp.x + dir.x * 1400.0f, tp.y + dir.y * 1400.0f, s.host->random().synced(),
                                        mv::kNoFacing, attack_move) == commands::RejectReason::None;
            CHECK(out.ordered);
            stage = 1;
            stage_frame = b->frame();
            continue;
        }
        if (b->frame() > stage_frame + 46) {
            stage = 9;
            continue;
        }
        out.acquired = out.acquired || cs.current_target(mem.front()) != tc::kNoObject;
    }
    return out;
}

// cmb1 t02 (BKR1 Q2 [H], TACTICAL_COMBAT §2.8 C): a land Order_Attack(ordered 1) on a unit that is not a team container /
// member / LUCKY_SHOT (the default test data: troopers without Create_Team) takes the weapon path: no ordered (unlocked)
// target slot, the weapon hard points / weapon aimed at the target, the target the formation's attack goal; path B locks it
// (ordered target, lock set) once it is inside weapon range.
struct LandOrderIssueRun {
    bool found = false, ordered = false, no_slot = false, goal_set = false, aimed = false, locked = false;
    uint32_t lock_frames = 0;
};

LandOrderIssueRun run_land_order_issue(const Data& d) {
    LandOrderIssueRun out;
    CampaignBattleOptions opt;
    opt.conflict.human_choice = galactic::BattleChoice::Fight;
    opt.battle.max_frames = 3000;
    opt.battle.skip_human_units = true;
    opt.battle.order_interval = 0;
    opt.battle.fog = false;
    ClashSession s(d, 0, opt, {"S10_Squad"}, {}, true);
    if (!s.gs) return out;
    int stage = 0;
    uint32_t stage_frame = 0;
    objects::ObjectId me = objects::kNoObjectId, target = objects::kNoObjectId;
    for (uint64_t t = 0; t < 3000 && stage < 9; ++t) {
        TacticalBattle* b = s.tick();
        TacticalMode* tm = s.host->tactical();
        if (!b || !tm || b->frame() < 3) {
            if (stage > 0) break;
            continue;
        }
        objects::ObjectStore& st = tm->primary();
        auto& cs = const_cast<tc::CombatSystem&>(b->combat());
        if (stage == 0) {
            for (size_t i = 0; i < cs.unit_count(); ++i) {
                const auto id = cs.unit_id_at(i);
                cs.make_invulnerable(id, 1000000, b->frame());
                const objects::ObjectHandle h = st.find(id);
                if (!st.alive(h) || cs.team(id) != 0 || !b->movement().has_unit(id)) continue;
                if (st.owner(h) == 0 && me == objects::kNoObjectId) me = id;
                if (st.owner(h) == 1 && target == objects::kNoObjectId) target = id;
            }
            out.found = me != objects::kNoObjectId && target != objects::kNoObjectId;
            CHECK(out.found);
            if (!out.found) break;
            const Vec3 tp = st.position(st.find(target)), ap = st.position(st.find(me));
            const float dx = ap.x - tp.x, dy = ap.y - tp.y, dl = std::sqrt(dx * dx + dy * dy);
            const Vec3 dir = dl > 1.0f ? Vec3{dx / dl, dy / dl, 0.0f} : Vec3{1.0f, 0.0f, 0.0f};
            CHECK(b->teleport(*tm, me, Vec3{tp.x + dir.x * 700.0f, tp.y + dir.y * 700.0f, ap.z}, st.facing(st.find(me))));
            const objects::ObjectId one[] = {me};
            out.ordered = b->order_attack(*tm, 0, one, target, s.host->random().synced()) == commands::RejectReason::None;
            out.no_slot = cs.ordered_target(me) == tc::kNoObject;
            out.goal_set = cs.attack_goal(me) == target;
            out.aimed = cs.weapon_aim(me) == target;
            stage = 1;
            stage_frame = b->frame();
            continue;
        }
        if (cs.ordered_target(me) == target && cs.order_locked(me)) {
            out.locked = true;
            out.lock_frames = b->frame() - stage_frame;
            stage = 9;
        } else if (b->frame() > stage_frame + 2000) {
            stage = 9;
        }
    }
    return out;
}

// #489 (FIGHTER_FLYING §4.11 B 1 / 2: the waypoint append 0x420f70, the arrival 0x423760 [H]): shift-queued land moves go into
// the squad's formation stack (a squad with no chase ranges, walking away from the enemy).
//   (i) a move to P1, then shift-queued P2 and P3: the stack holds three blocks and the queue view lists P2, P3; the squad
//       walks to P1, then P2, then P3;
//   (ii) a fourth shift-click within 20 of P1 (the player order's own block, 0x04; given with the others) closes a patrol
//       loop over the whole stack (no fourth block), which survives a battle state round trip: after P3 the squad walks
//       back to P1, then P2 and P3 again;
//   (iii) a fresh move and seven shift-queued points: six are kept (MaxWaypointsPerPath 6), the seventh is refused.
struct LandWaypointRun {
    bool found = false, stacked = false, listed = false, looped = false, saved = false, capped = false;
    std::vector<int> visits;  // the points the squad's goal named, in order (1..3)
};

std::unique_ptr<Data> waypoint_data() {
    static std::string prefix;
    const char* const old = g_temp_prefix;
    prefix = std::string(old) + "_land_waypoints";
    g_temp_prefix = prefix.c_str();
    auto d = make_data(false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false,
                       false, false, true);
    g_temp_prefix = old;
    return d;
}

LandWaypointRun run_land_waypoints(const Data& d) {
    LandWaypointRun out;
    CampaignBattleOptions opt;
    opt.conflict.human_choice = galactic::BattleChoice::Fight;
    opt.battle.max_frames = 9000;
    opt.battle.skip_human_units = true;
    opt.battle.order_interval = 0;
    opt.battle.fog = false;
    ClashSession s(d, 0, opt, {"S10_Squad"}, {}, true);
    if (!s.gs) return out;
    objects::ObjectId sq = objects::kNoObjectId;
    std::vector<objects::ObjectId> mem;
    mv::V2 p[4], home;
    bool ordered = false;
    for (uint64_t t = 0; t < 9000; ++t) {
        TacticalBattle* b = s.tick();
        TacticalMode* tm = s.host->tactical();
        if (!b || !tm || b->frame() < 3) continue;
        objects::ObjectStore& st = tm->primary();
        const tc::CombatSystem& cs = b->combat();
        const mv::MovementSystem& ms = b->movement();
        SimRng& rng = s.host->random().synced();
        if (!ordered) {
            ordered = true;
            Vec3 epos{};
            bool enemy = false;
            std::map<uint32_t, std::vector<objects::ObjectId>> teams;
            for (size_t i = 0; i < cs.unit_count(); ++i) {
                const auto id = cs.unit_id_at(i);
                if (cs.team(id) != 0 && cs.alive(id)) teams[cs.team(id)].push_back(id);
            }
            for (const auto& [tid, m] : teams) {
                const auto cid = static_cast<objects::ObjectId>(tid);
                const objects::ObjectHandle th = st.find(cid);
                if (m.size() < 2 || !st.alive(th) || !ms.has_unit(cid)) continue;
                if (st.owner(th) == 0 && sq == objects::kNoObjectId) {
                    sq = cid;
                    mem = m;
                } else if (st.owner(th) == 1 && !enemy) {
                    enemy = true;
                    epos = st.position(th);
                }
            }
            out.found = sq != objects::kNoObjectId && enemy;
            CHECK(out.found);
            if (!out.found) break;
            // away from the enemy: P1 150 ahead, P2 150 to the side, P3 150 further ahead
            const Vec3 sp = st.position(st.find(sq));
            home = {sp.x, sp.y};
            float dx = sp.x - epos.x, dy = sp.y - epos.y;
            const float len = std::max(1.0f, std::sqrt(dx * dx + dy * dy));
            dx /= len;
            dy /= len;
            p[1] = {sp.x + 150.0f * dx, sp.y + 150.0f * dy};
            p[2] = {p[1].x - 150.0f * dy, p[1].y + 150.0f * dx};
            p[3] = {p[2].x + 150.0f * dx, p[2].y + 150.0f * dy};
            CHECK(b->order_move(*tm, 0, mem, p[1].x, p[1].y, rng, mv::kNoFacing, false, false, mv::kMoveFlagPlayerOrder) ==
                  commands::RejectReason::None);
            CHECK(b->order_move(*tm, 0, mem, p[2].x, p[2].y, rng, mv::kNoFacing, false, true) == commands::RejectReason::None);
            CHECK(b->order_move(*tm, 0, mem, p[3].x, p[3].y, rng, mv::kNoFacing, false, true) == commands::RejectReason::None);
            const LandFormationRecord* r = record_of(*b, sq);
            out.stacked = r && r->blocks.size() == 3;
            const auto q = b->queued_orders(mem.front());
            out.listed = q.size() == 2 && q[0].x == p[2].x && q[1].x == p[3].x;
            CHECK(b->order_move(*tm, 0, mem, p[1].x + 5.0f, p[1].y + 5.0f, rng, mv::kNoFacing, false, true) ==
                  commands::RejectReason::None);
            r = record_of(*b, sq);
            out.looped = r && r->blocks.size() == 3 && r->blocks[0].loop && r->blocks[1].loop && r->blocks[2].loop;
            // the looped three-block stack through a battle state round trip
            const std::vector<mv::FighterBlock> before = r ? r->blocks : std::vector<mv::FighterBlock>{};
            StateHasher h0;
            b->hash(h0);
            ByteWriter w;
            b->write_part(TacticalBattle::SavePart::State, w);
            ByteReader rd(w.bytes());
            const bool read = b->read_part(TacticalBattle::SavePart::State, rd);
            StateHasher h1;
            b->hash(h1);
            r = record_of(*b, sq);
            out.saved = read && r && r->blocks.size() == before.size() && h0.digest() == h1.digest();
            for (size_t k = 0; out.saved && k < before.size(); ++k) out.saved = mv::same_block(before[k], r->blocks[k]);
            continue;
        }
        const mv::UnitView v = ms.view(sq);
        if (v.has_goal) {
            int at = 0;
            for (int k = 1; k <= 3; ++k)
                if (std::hypot(v.goal.x - p[k].x, v.goal.y - p[k].y) < 60.0f) at = k;
            if (at != 0 && (out.visits.empty() || out.visits.back() != at)) out.visits.push_back(at);
        }
        if (out.visits.size() >= 6) {
            // (iii) a fresh move to P3, then seven queued points 40 apart on the way back to the start (no loop)
            CHECK(b->order_move(*tm, 0, mem, p[3].x, p[3].y, rng) == commands::RejectReason::None);
            const float hx = home.x - p[3].x, hy = home.y - p[3].y, hl = std::max(1.0f, std::sqrt(hx * hx + hy * hy));
            for (int k = 1; k <= 7; ++k) {
                const float f = 40.0f * static_cast<float>(k) / hl;
                (void)b->order_move(*tm, 0, mem, p[3].x + f * hx, p[3].y + f * hy, rng, mv::kNoFacing, false, true);
            }
            const LandFormationRecord* r = record_of(*b, sq);
            out.capped = r && r->blocks.size() == 7 && b->queued_orders(mem.front()).size() == 6;
            break;
        }
    }
    return out;
}

// #491 (Pop 0x4105a0's member loop [H]): a squad whose troopers carry BARRAGE walks a move to P1 and one shift-queued waypoint
// P2. After the orders the test gives the order's block flag 0x40 (no port writer sets it) and switches BARRAGE on for every
// trooper. While it walks, each trooper's Pop facing (locomotor vf +0x1e8) is its own yaw (a plain Walk) and BARRAGE stays on.
// At the arrival pop of the order's block: every trooper's BARRAGE is off (the block lacks 0x10), signal 0x2a was raised and
// the orientation set once per member. With 0x10 also given to the block, BARRAGE stays on through the pop.
struct LandPopEffectsRun {
    bool found = false, active = false, facing = true, on_before = true, popped = false, off = false, signals = false, oriented = false;
};

std::string barrage_trooper(std::string t) {
    const char* at = "<GroundInfantry Name=\"S10_Trooper\">";
    if (const size_t p = t.find(at); p != std::string::npos)
        t.insert(p + std::strlen(at), "<Unit_Abilities_Data SubObjectList=\"Yes\"><Unit_Ability><Type>BARRAGE</Type></Unit_Ability>"
                                      "</Unit_Abilities_Data>");
    return t;
}

std::unique_ptr<Data> barrage_data() {
    static std::string prefix;
    const char* const old = g_temp_prefix;
    prefix = std::string(old) + "_land_pop_barrage";
    g_temp_prefix = prefix.c_str();
    g_types_patch = barrage_trooper;
    auto d = make_data(false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false,
                       false, false, true);
    g_types_patch = nullptr;
    g_temp_prefix = old;
    return d;
}

LandPopEffectsRun run_land_pop_effects(const Data& d, uint8_t extra_flags) {
    LandPopEffectsRun out;
    CampaignBattleOptions opt;
    opt.conflict.human_choice = galactic::BattleChoice::Fight;
    opt.battle.max_frames = 4000;
    opt.battle.skip_human_units = true;
    opt.battle.order_interval = 0;
    opt.battle.fog = false;
    ClashSession s(d, 0, opt, {"S10_Squad"}, {}, true);
    if (!s.gs) return out;
    objects::ObjectId sq = objects::kNoObjectId;
    std::vector<objects::ObjectId> mem;
    bool ordered = false;
    for (uint64_t t = 0; t < 4000; ++t) {
        TacticalBattle* b = s.tick();
        TacticalMode* tm = s.host->tactical();
        if (!b || !tm || b->frame() < 3) continue;
        objects::ObjectStore& st = tm->primary();
        const tc::CombatSystem& cs = b->combat();
        const mv::MovementSystem& ms = b->movement();
        SimRng& rng = s.host->random().synced();
        if (!ordered) {
            ordered = true;
            Vec3 epos{};
            bool enemy = false;
            std::map<uint32_t, std::vector<objects::ObjectId>> teams;
            for (size_t i = 0; i < cs.unit_count(); ++i) {
                const auto id = cs.unit_id_at(i);
                if (cs.team(id) != 0 && cs.alive(id)) teams[cs.team(id)].push_back(id);
            }
            for (const auto& [tid, m] : teams) {
                const auto cid = static_cast<objects::ObjectId>(tid);
                const objects::ObjectHandle th = st.find(cid);
                if (m.size() < 2 || !st.alive(th) || !ms.has_unit(cid)) continue;
                if (st.owner(th) == 0 && sq == objects::kNoObjectId) {
                    sq = cid;
                    mem = m;
                } else if (st.owner(th) == 1 && !enemy) {
                    enemy = true;
                    epos = st.position(th);
                }
            }
            out.found = sq != objects::kNoObjectId && enemy;
            CHECK(out.found);
            if (!out.found) break;
            const Vec3 sp = st.position(st.find(sq));
            float dx = sp.x - epos.x, dy = sp.y - epos.y;
            const float len = std::max(1.0f, std::sqrt(dx * dx + dy * dy));
            dx /= len;
            dy /= len;
            CHECK(b->order_move(*tm, 0, mem, sp.x + 100.0f * dx, sp.y + 100.0f * dy, rng, mv::kNoFacing, false, false,
                                mv::kMoveFlagPlayerOrder) == commands::RejectReason::None);
            CHECK(b->order_move(*tm, 0, mem, sp.x + 100.0f * dx - 100.0f * dy, sp.y + 100.0f * dy + 100.0f * dx, rng, mv::kNoFacing,
                                false, true) == commands::RejectReason::None);
            for (LandFormationRecord& r : TacticalBattleTestAccess::land_formations(*b))
                if (r.mover == sq && r.blocks.size() == 2) r.blocks.back().flags = static_cast<uint8_t>(r.blocks.back().flags | 0x40 | extra_flags);
            tactical::abilities::AbilitySystem& ab = TacticalBattleTestAccess::abilities(*b);
            out.active = true;
            for (const objects::ObjectId u : mem) {
                (void)ab.set_unit_ability_active(u, 5, true, b->frame());
                out.active = out.active && ab.is_active(u, 5);
            }
            continue;
        }
        if (b->stats().land_waypoint_pops == 0) {
            for (const objects::ObjectId u : mem) {
                if (ms.has_unit(u)) out.facing = out.facing && ms.pop_facing(u) == ms.view(u).yaw;
                out.on_before = out.on_before && b->abilities().is_active(u, 5);  // on until the pop
            }
            continue;
        }
        out.popped = true;
        out.off = true;
        for (const objects::ObjectId u : mem) out.off = out.off && !b->abilities().is_active(u, 5);
        out.signals = b->stats().land_pop_signals == mem.size();
        out.oriented = b->stats().land_pop_facings == mem.size();
        break;
    }
    return out;
}

void test_land_auto_engage() {
    {
        auto d = chase_data(true);
        const LandAutoEngageRun r = run_land_auto_engage(*d);
        std::printf("  land Auto_Engage: never ordered -> no target, no push %d; ordered %d, arrived %d, pushed onto the enemy %d after "
                    "%u frame(s), walking toward it %d; enemy gone -> popped %d after %u frame(s), order resumed %d\n",
                    r.idle_none ? 1 : 0, r.ordered ? 1 : 0, r.arrived ? 1 : 0, r.pushed ? 1 : 0, r.push_frames,
                    r.moved_toward ? 1 : 0, r.popped ? 1 : 0, r.pop_frames, r.resumed ? 1 : 0);
        CHECK(r.found && r.idle_none && r.ordered && r.arrived && r.pushed && r.push_frames <= 120 && r.moved_toward);
        CHECK(r.popped && r.pop_frames <= 61 && r.resumed);
        CHECK(r.push_hp == -1);  // no hard points, no hard point named
        // cmb2 t04 (FIGHTER_FLYING §4.11 B, BKT1_ATTACK_RE §1.3 [H]): the troopers carry a targetable hard point -> the Push
        // names one (0x394610); its position is the block's goal point and the engagement adds 0x397780(T)
        auto dh = chase_data(true, true);
        const LandAutoEngageRun h = run_land_auto_engage(*dh);
        std::printf("  land Auto_Engage with a hard point on the target: pushed %d, hard point %d (without: %d)\n", h.pushed ? 1 : 0,
                    h.push_hp, r.push_hp);
        CHECK(h.pushed && h.push_hp >= 0);
    }
    {
        auto d = chase_data(true);
        const LandStep10Run r = run_land_step10(*d);
        std::printf("  land step-10 attack block (cmb2 t07): no record -> refused %d; ordered -> pushed %d, attack block %d, attack goal %d; "
                    "again refused %d, friendly refused %d; two-block stack saved %d\n",
                    r.no_record_refused ? 1 : 0, r.pushed ? 1 : 0, r.block ? 1 : 0, r.goal ? 1 : 0, r.again_refused ? 1 : 0,
                    r.friend_refused ? 1 : 0, r.saved ? 1 : 0);
        CHECK(r.found && r.no_record_refused && r.pushed && r.block && r.goal && r.again_refused && r.friend_refused);
        CHECK(r.saved);
    }
    {
        auto d = chase_data(true);
        const LandScanMovingRun plain = run_land_scan_moving(*d, false);
        const LandScanMovingRun am = run_land_scan_moving(*d, true);
        std::printf("  land scan range while moving: plain Move_To acquired %d (cube 0), Attack_Move acquired %d (R + Rr)\n",
                    plain.acquired ? 1 : 0, am.acquired ? 1 : 0);
        CHECK(plain.found && plain.ordered && !plain.acquired);
        CHECK(am.found && am.ordered && am.acquired);
    }
    {
        auto d = chase_data(false);
        const LandOrderIssueRun r = run_land_order_issue(*d);
        std::printf("  land Order_Attack (weapon path): ordered %d, no ordered slot %d, attack goal %d, weapon aim %d; path B locked "
                    "it %d after %u frame(s)\n",
                    r.ordered ? 1 : 0, r.no_slot ? 1 : 0, r.goal_set ? 1 : 0, r.aimed ? 1 : 0, r.locked ? 1 : 0, r.lock_frames);
        CHECK(r.found && r.ordered && r.no_slot && r.goal_set && r.aimed && r.locked);
    }
    {
        auto d = waypoint_data();
        const LandWaypointRun r = run_land_waypoints(*d);
        std::string seq;
        for (const int k : r.visits) seq += static_cast<char>('0' + k);
        std::printf("  land shift-queued waypoints (#489): stack of 3 %d, queue view %d, patrol loop %d, saved %d; walked %s; 7th "
                    "refused %d\n",
                    r.stacked ? 1 : 0, r.listed ? 1 : 0, r.looped ? 1 : 0, r.saved ? 1 : 0, seq.c_str(), r.capped ? 1 : 0);
        CHECK(r.found && r.stacked && r.listed && r.looped && r.saved && r.capped);
        CHECK(seq.rfind("123123", 0) == 0);  // P1, P2, P3, then the loop P1, P2, P3
    }
    {
        auto d = barrage_data();
        const LandPopEffectsRun r = run_land_pop_effects(*d, 0);
        const LandPopEffectsRun k = run_land_pop_effects(*d, 0x10);
        std::printf("  land Pop member loop (#491): BARRAGE on %d, still on before the pop %d; Pop facing = own yaw %d; popped %d: "
                    "BARRAGE off %d, signal 0x2a %d and orientation %d per member; with 0x10 BARRAGE kept %d\n",
                    r.active ? 1 : 0, r.on_before ? 1 : 0, r.facing ? 1 : 0, r.popped ? 1 : 0, r.off ? 1 : 0, r.signals ? 1 : 0,
                    r.oriented ? 1 : 0, k.popped && !k.off ? 1 : 0);
        CHECK(r.found && r.active && r.on_before && r.facing && r.popped && r.off && r.signals && r.oriented);
        CHECK(k.found && k.active && k.on_before && k.popped && !k.off && k.signals && k.oriented);
    }
}

}  // namespace bo3split
