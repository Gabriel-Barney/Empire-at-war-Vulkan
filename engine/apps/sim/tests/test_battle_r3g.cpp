// alamo_sim_r3g_tests: r3g "Animation-timed gameplay" on synthetic land battles (battle_test_fixture.hpp, no game data).
//   * t01 (A1_READINGS §35.2): a timed land Reinforce whose landing object has Land / Takeoff clips (151 frames at 30 fps,
//     played at 1.15) unloads only once Land has passed 98 % (129 frames after the clearance), and is removed only once
//     Takeoff has passed 98 % after the finish. Without the clips (the other landing tests) both pass at once.
//   * t05 (MOVEMENT §5.5, S9_DEATH_CLONE_SHIELDS §1.4): a crush with no Squash_Damage_Type kills with kind 14
//     (Damage_Crush, the type default 0x367250 writes) through the victim's Damage_Crush clone, whose death clip is Crushed;
//     a crusher whose Squash_Damage_Type names another tDamageType kills with that kind.
//   * t06 (TACTICAL_GAPS §6.2): a trooper under SPREAD_OUT idles on Idle Crouch (71); off again, on Idle.
//   * #44 t01 (R3G_ANIM_TIMED_RE §2 / §4): DEPLOY_TROOPERS through the battle host (troopers hang on the walker's bones,
//     TrooperReleased frees them, the clip timing of the ability system); a cable trip kills a walking walker through its
//     Damage_Cable_Attack (CA_DIE) clone; #44 t03: a speeder stopped during the pull lets the walker go.
#include "battle_test_fixture.hpp"

#include <functional>
#include "tactical_battle_test_access.hpp"  // ref3 t06
#include "alamo/tactical/abilities/ability_types.hpp"
#include "alamo/tactical/combat/combat_system.hpp"
#include "alamo/tactical/movement/movement_system.hpp"
#include "alamo/tactical/services/stream_rng.hpp"

namespace {


// A land battle on S10_B (invaded by player 1's S10_Squad company from planet 2). `per_tick(b, tm)` runs after every host
// tick once the battle exists; it returns false to stop. `animation` (may be null) is attached. `edit` (may be empty)
// changes the galactic setup first (#44 t01: extra defender forces on S10_B), `opt_edit` (may be empty) the battle options.
template <class F>
void land_battle(Data& d, SimAnimation* animation, uint32_t max_frames, F per_tick, bool skirmish = false,
                 const std::function<void(galactic::GalacticSetup&)>& edit = {},
                 const std::function<void(CampaignBattleOptions&)>& opt_edit = {}, uint32_t workers = 0) {
    jobs::JobSystem js(jobs::JobSystemConfig{static_cast<int32_t>(workers)});
    SimConfig cfg;
    cfg.seed = 2026;
    cfg.players = 4;
    SimHost host(d.types, js, cfg);
    galactic::GalacticSetup setup = make_setup(d, true);
    if (edit) edit(setup);
    galactic::GalacticSim* gs = host.start_galactic(d.rules, std::move(setup));
    CHECK(gs != nullptr);
    if (!gs) return;
    CampaignBattleOptions opt;
    opt.conflict.human_choice = galactic::BattleChoice::Fight;
    opt.battle.max_frames = max_frames;
    opt.battle.landing_unload_timed = true;
    opt.battle.skirmish = skirmish;  // mcx4 tac2: the skirmish landing clearance delay
    if (opt_edit) opt_edit(opt);
    if (animation) {
        animation->attach(host, false);
        opt.animation = animation;
    }
    CampaignBattles battles(host, d.crules, *d.catalog,
                            [](const std::string& name, terrain::MapData& m) {
                                if (name != "S10_Land_B") return false;
                                m = land_map();
                                return true;
                            },
                            opt);
    CHECK(battles.attach(nullptr));
    std::vector<objects::ObjectHandle> up;
    for (objects::ObjectHandle h : gs->planets()[2].surface)
        if (gs->store().owner(h) == 1) up.push_back(h);
    CHECK(gs->launch_units(1, 2, up) == commands::RejectReason::None);
    const std::vector<uint32_t> uf = gs->fleets_at(2, 1);
    if (!uf.empty()) CHECK(gs->move_fleet(1, uf[0], 1) == commands::RejectReason::None);
    bool invaded = false, seen = false;
    for (uint64_t t = 0; t < 6000; ++t) {
        host.tick_once();
        if (!invaded && host.modes().depth() == 1) {
            const std::vector<uint32_t> f = gs->fleets_at(1, 1);
            if (!f.empty() && !gs->fleets()[f[0]].moving) invaded = gs->invade(1, 1, {}) == commands::RejectReason::None;
        }
        TacticalBattle* b = battles.battle();
        TacticalMode* tm = host.tactical();
        if (!b || !tm) {
            if (seen) break;
            continue;
        }
        seen = true;
        if (!per_tick(*b, *tm, host)) break;
    }
    CHECK(seen);
    if (animation) animation->detach(host);
}

// ---- t01 ----
void test_landing_clips() {
#ifdef ALAMO_SIM_HAVE_ANIM
    g_temp_prefix = "alamo_sim_r3g_tests";
    auto d = make_data(false, false, false, false, false, false, false, false, false, false, false, true);
    const objects::TypeIndex landing_t = d->types.resolve_name("S10_Lander_Landing");
    CHECK(landing_t != objects::kNone);
    const auto model = synth_model("R3G_LANDER", {{anim::kAnimIdle, 30u}, {0x2b, 151u}, {0x2c, 151u}});
    SimAnimation animation(d->types, *d->files);
    animation.set_model(landing_t, objects::ModeKind::Land, model.get());
    namespace mv = alamo::tactical::movement;
    bool dropped = false;
    objects::ObjectId landing = objects::kNoObjectId;
    uint32_t drop = 0, spawned_before = 0;
    int team0 = -1, team1 = -1, finish = -1, removed = -1;
    land_battle(*d, &animation, 1500, [&](TacticalBattle& b, TacticalMode& tm, SimHost& host) {
        objects::ObjectStore& st = tm.primary();
        if (!dropped) {
            if (b.frame() != 1) return true;
            const auto reserves = b.reserves(1);
            CHECK(reserves.size() == 1);
            if (reserves.empty()) return false;
            const objects::ObjectId key = reserves[0].galactic_id;
            spawned_before = b.stats().spawned;
            if (b.fog()) alamo::sim::TacticalBattleTestAccess::fog(b)->reveal_all(1);
            for (int32_t cy = 6; cy < 58 && !dropped; cy += 2)
                for (int32_t cx = 6; cx < 58 && !dropped; cx += 2) {
                    const mv::V2 c = mv::cell_center(cx, cy);
                    if (!b.land_drop_valid(tm, 1, key, c.x, c.y, false, false)) continue;
                    if (b.order_reinforce(tm, 1, key, c.x, c.y, host.random().synced()) != commands::RejectReason::None) continue;
                    dropped = true;
                    drop = b.frame();
                    for (const objects::ObjectHandle h : st.all_objects())
                        if (st.alive(h) && st.type(h) == landing_t) landing = st.id(h);
                }
            CHECK(dropped && landing != objects::kNoObjectId);
            return dropped;
        }
        const int since = static_cast<int>(b.frame() - drop);
        const uint32_t spawned = b.stats().spawned - spawned_before;
        if (team0 < 0 && spawned >= 2) team0 = since;
        if (team1 < 0 && spawned >= 4) team1 = since;
        if (finish < 0 && b.landings_in_flight(1) == 0) finish = since;
        if (removed < 0 && !st.alive(st.find(landing))) removed = since;
        return removed < 0 && since < 900;
    });
    // Clearance at +1 starts Land: 98 % of 151 frames at 1.15 is passed 129 frames later (+130, the step timer starts);
    // teams at +190 / +250 (2 s steps), the finish at +310 starts Takeoff; removed 129 frames later (+439). Without the
    // clips: +61 / +121 / +181 (test_landing_drop).
    std::printf("  r3g landing clips: team 0 +%d, team 1 +%d, finish +%d, removed +%d\n", team0, team1, finish, removed);
    CHECK(team0 == 190 && team1 == 250 && finish == 310 && removed == 439);
#else
    std::printf("  r3g landing clips: skipped (no engine/anim)\n");
#endif
}

// #128 t01: the landing service on counter-based streams (bit StreamId::Landing): a landing's unload steps (their deploy
// and exit move) draw from the landing's own stream. Off: the synced draws; on: the run differs and is identical at
// 0 / 1 / 4 / 15 workers and in every service run mode.
uint64_t run_landing_streams(Data& d, uint32_t streams, uint32_t workers, tactical::services::RunMode run_mode, uint32_t& spawned) {
    namespace mv = alamo::tactical::movement;
    const TacticalBattle::ServiceRunDefaults saved = TacticalBattle::service_run_defaults();
    TacticalBattle::ServiceRunDefaults sd = saved;
    sd.streams = streams;
    sd.mode = run_mode;
    TacticalBattle::set_service_run_defaults(sd);
    StateHasher all;
    bool dropped = false;
    uint32_t drop = 0, before = 0;
    spawned = 0;
    land_battle(
        d, nullptr, 900,
        [&](TacticalBattle& b, TacticalMode& tm, SimHost& host) {
            objects::ObjectStore& st = tm.primary();
            if (!dropped) {
                if (b.frame() != 1) return true;
                const auto reserves = b.reserves(1);
                if (reserves.empty()) return false;
                before = b.stats().spawned;
                if (b.fog()) alamo::sim::TacticalBattleTestAccess::fog(b)->reveal_all(1);
                for (int32_t cy = 6; cy < 58 && !dropped; cy += 2)
                    for (int32_t cx = 6; cx < 58 && !dropped; cx += 2) {
                        const mv::V2 c = mv::cell_center(cx, cy);
                        if (!b.land_drop_valid(tm, 1, reserves[0].galactic_id, c.x, c.y, false, false)) continue;
                        if (b.order_reinforce(tm, 1, reserves[0].galactic_id, c.x, c.y, host.random().synced()) !=
                            commands::RejectReason::None)
                            continue;
                        dropped = true;
                        drop = b.frame();
                    }
                return dropped;
            }
            all.u32(b.frame());
            all.u32(host.random().synced().state());
            for (const objects::ObjectHandle h : st.all_objects()) {
                if (!st.alive(h)) continue;
                const Vec3 p = st.position(h);
                all.u32(static_cast<uint32_t>(st.id(h)));
                all.f32(p.x);
                all.f32(p.y);
            }
            spawned = b.stats().spawned - before;
            return b.frame() < drop + 400;
        },
        false, {}, {}, workers);
    TacticalBattle::set_service_run_defaults(saved);
    return all.digest();
}

void test_landing_streams() {
    g_temp_prefix = "alamo_sim_r3g_tests_128";
    auto d = make_data(false, false, false, false, false, false, false, false, false, false, false, true);
    const uint32_t bit = 1u << static_cast<uint32_t>(tactical::services::StreamId::Landing);
    const auto par = TacticalBattle::service_run_defaults().mode;
    uint32_t n_off = 0, n_on = 0, n = 0;
    const uint64_t off = run_landing_streams(*d, 0, 0, par, n_off);
    const uint64_t on = run_landing_streams(*d, bit, 0, par, n_on);
    std::printf("  landing streams: off %016llx (%u spawned), on %016llx (%u spawned)\n", static_cast<unsigned long long>(off), n_off,
                static_cast<unsigned long long>(on), n_on);
    CHECK(n_off >= 4 && n_on >= 4);  // every unload step ran
    CHECK(off != on);                // the draws moved
    for (const uint32_t w : {1u, 4u, 15u}) CHECK(run_landing_streams(*d, bit, w, par, n) == on);
    for (const auto m : {tactical::services::RunMode::Serial, tactical::services::RunMode::Reverse})
        CHECK(run_landing_streams(*d, bit, 4, m, n) == on);
    CHECK(run_landing_streams(*d, 0, 4, par, n) == off);  // off stays worker-independent too
}

// #128 t04: the queues service on counter-based streams (bit StreamId::Queues): an order group a queue starts draws from
// its own stream (keyed by the group and the queue pass). Player 1's squads get a move, then a queued move and a queued
// attack: the queue service starts each once the one before ends, and a standing squad container's move draws its
// members' start delays. Off: the synced draws; on: the run differs and is identical at 0 / 1 / 4 / 15 workers and in
// every service run mode.
struct QueueStreamRun {
    uint64_t digest = 0;
    uint32_t started = 0;  // the frame the last queued order (the attack) left the queue (0: never)
};
QueueStreamRun run_queue_streams(Data& d, uint32_t streams, uint32_t workers, tactical::services::RunMode run_mode) {
    namespace mv = alamo::tactical::movement;
    const TacticalBattle::ServiceRunDefaults saved = TacticalBattle::service_run_defaults();
    TacticalBattle::ServiceRunDefaults sd = saved;
    sd.streams = streams;
    sd.mode = run_mode;
    TacticalBattle::set_service_run_defaults(sd);
    QueueStreamRun r;
    StateHasher all;
    std::vector<objects::ObjectId> ids;
    land_battle(
        d, nullptr, 1500,
        [&](TacticalBattle& b, TacticalMode& tm, SimHost& host) {
            objects::ObjectStore& st = tm.primary();
            if (ids.empty()) {
                if (b.frame() != 30) return true;
                for (const objects::ObjectHandle h : st.owned_by(1))
                    if (st.alive(h) && b.movement().has_unit(st.id(h))) ids.push_back(st.id(h));
                objects::ObjectId foe = objects::kNoObjectId;  // the first enemy unit
                for (const objects::ObjectHandle h : st.all_objects())
                    if (foe == objects::kNoObjectId && st.alive(h) && st.owner(h) != 1 && b.movement().has_unit(st.id(h)))
                        foe = st.id(h);
                if (ids.empty()) return false;
                SimRng& rng = host.random().synced();
                const mv::V2 a = mv::cell_center(28, 28), c = mv::cell_center(36, 28);
                CHECK(b.order_move(tm, 1, ids, a.x, a.y, rng) == commands::RejectReason::None);
                CHECK(b.order_move(tm, 1, ids, c.x, c.y, rng, mv::kNoFacing, false, true) ==
                      commands::RejectReason::None);
                CHECK(b.order_attack(tm, 1, ids, foe, rng, -1, true) == commands::RejectReason::None);
                return true;
            }
            if (!r.started && b.queued_orders(ids.front()).empty()) r.started = b.frame();  // the attack started
            all.u32(b.frame());
            all.u32(host.random().synced().state());
            for (const objects::ObjectHandle h : st.all_objects()) {
                if (!st.alive(h)) continue;
                const Vec3 p = st.position(h);
                all.u32(static_cast<uint32_t>(st.id(h)));
                all.f32(p.x);
                all.f32(p.y);
            }
            return !r.started || b.frame() < r.started + 300;
        },
        false, {}, {}, workers);
    TacticalBattle::set_service_run_defaults(saved);
    r.digest = all.digest();
    return r;
}

void test_queue_streams() {
    g_temp_prefix = "alamo_sim_r3g_tests_128q";
    // squads: TEAM containers on their own locomotor (a standing container's move draws its members' start delays)
    auto d = make_data(false, false, false, false, false, false, true, false, false, false, false, false, false, false,
                       false, false, false, false, true);
    const uint32_t bit = 1u << static_cast<uint32_t>(tactical::services::StreamId::Queues);
    const auto par = TacticalBattle::service_run_defaults().mode;
    const QueueStreamRun off = run_queue_streams(*d, 0, 0, par);
    const QueueStreamRun on = run_queue_streams(*d, bit, 0, par);
    std::printf("  queue streams: off %016llx (queued attack at frame %u), on %016llx (frame %u)\n",
                static_cast<unsigned long long>(off.digest), off.started, static_cast<unsigned long long>(on.digest),
                on.started);
    CHECK(off.started > 30 && on.started > 30);  // the queue service started the queued orders
    CHECK(off.digest != on.digest);              // the draws moved
    for (const uint32_t w : {1u, 4u, 15u}) CHECK(run_queue_streams(*d, bit, w, par).digest == on.digest);
    for (const auto m : {tactical::services::RunMode::Serial, tactical::services::RunMode::Reverse})
        CHECK(run_queue_streams(*d, bit, 4, m).digest == on.digest);
    CHECK(run_queue_streams(*d, 0, 4, par).digest == off.digest);  // off stays worker-independent too
}

// ---- t05 ----
std::string crush_patch(std::string t) {
    // The trooper gets a Damage_Crush clone (Crushed, as the 21 FoC Damage_Crush clones) and a Damage_Normal clone.
    const std::string at = "<GroundInfantry Name=\"S10_Trooper\">";
    if (const size_t p = t.find(at); p != std::string::npos)
        t.insert(p + at.size(), "<Death_Clone>Damage_Crush, S10_Trooper_Crush_Clone</Death_Clone>"
                                "<Death_Clone>Damage_Normal, S10_Trooper_Normal_Clone</Death_Clone>");
    const size_t end = t.find("</S10>");
    if (end != std::string::npos)
        t.insert(end, " <GroundInfantry Name=\"S10_Trooper_Crush_Clone\"><Specific_Death_Anim_Type>Crushed</Specific_Death_Anim_Type>"
                      "<Death_Persistence_Duration>5</Death_Persistence_Duration><LandBehavior>HIDE_WHEN_FOGGED</LandBehavior></GroundInfantry>\n"
                      " <GroundInfantry Name=\"S10_Trooper_Normal_Clone\"><Death_Persistence_Duration>5</Death_Persistence_Duration>"
                      "<LandBehavior>HIDE_WHEN_FOGGED</LandBehavior></GroundInfantry>\n");
    return t;
}

void test_crush_kind() {
    g_temp_prefix = "alamo_sim_r3g_tests_crush";
    g_types_patch = crush_patch;
    auto d = make_data();
    g_types_patch = nullptr;
    const objects::TypeIndex trooper = d->types.resolve_name("S10_Trooper"), crush_clone = d->types.resolve_name("S10_Trooper_Crush_Clone");
    CHECK(trooper != objects::kNone && crush_clone != objects::kNone);
    CHECK(d->types.get_string(crush_clone, "Specific_Death_Anim_Type").find("Crushed") != std::string_view::npos);
    objects::ObjectId victim = objects::kNoObjectId, crusher = objects::kNoObjectId;
    uint8_t kind = 0;
    int32_t clone = -2;
    land_battle(*d, nullptr, 600, [&](TacticalBattle& b, TacticalMode& tm, SimHost&) {
        objects::ObjectStore& st = tm.primary();
        {
            if (b.frame() < 2) return true;
            for (const objects::ObjectHandle h : st.all_objects()) {
                if (!st.alive(h) || !b.combat().has_unit(st.id(h)) || !b.combat().alive(st.id(h))) continue;
                if (victim == objects::kNoObjectId && st.type(h) == trooper) victim = st.id(h);
                else if (crusher == objects::kNoObjectId) crusher = st.id(h);
            }
            CHECK(victim != objects::kNoObjectId && crusher != objects::kNoObjectId);
            if (victim == objects::kNoObjectId || crusher == objects::kNoObjectId) return false;
            CHECK(alamo::sim::TacticalBattleTestAccess::crush(b, crusher, victim));
            // The kill is reported at once (combat deaths() of this frame; the next tick clears them).
            for (const tactical::combat::DeathEvent& e : b.combat().deaths())
                if (e.id == victim) {
                    kind = e.kind;
                    clone = e.death_clone;
                }
        }
        return false;
    });
    std::printf("  r3g crush: kind %u, clone %d (Damage_Crush clone %d)\n", static_cast<unsigned>(kind), clone, static_cast<int>(crush_clone));
    CHECK(kind == 14 && clone == static_cast<int32_t>(crush_clone));
}

// ---- t06 ----
std::string spread_patch(std::string t) {
    // IDLE (the idle service runs only for it) and a SPREAD_OUT unit ability on the trooper. Attack range 1: with a
    // target held the IDLE service changes nothing (0x62e570 step 4), so the troopers must not engage.
    const std::string beh = "<Behavior>SELECTABLE, WEAPON</Behavior>";
    if (const size_t p = t.find(beh); p != std::string::npos) t.replace(p, beh.size(), "<Behavior>SELECTABLE, WEAPON, IDLE</Behavior>");
    const std::string range = "<Targeting_Max_Attack_Distance>250</Targeting_Max_Attack_Distance>";
    if (const size_t p = t.find(range); p != std::string::npos)
        t.replace(p, range.size(), "<Targeting_Max_Attack_Distance>1</Targeting_Max_Attack_Distance>");
    const std::string at = "<GroundInfantry Name=\"S10_Trooper\">";
    if (const size_t p = t.find(at); p != std::string::npos)
        t.insert(p + at.size(), "<Unit_Abilities_Data SubObjectList=\"Yes\"><Unit_Ability><Type>SPREAD_OUT</Type>"
                                "<Recharge_Seconds>1</Recharge_Seconds></Unit_Ability></Unit_Abilities_Data>");
    return t;
}

void test_crouch() {
#ifdef ALAMO_SIM_HAVE_ANIM
    g_temp_prefix = "alamo_sim_r3g_tests_crouch";
    g_types_patch = spread_patch;
    auto d = make_data();
    g_types_patch = nullptr;
    const objects::TypeIndex trooper = d->types.resolve_name("S10_Trooper");
    const auto model = synth_model("R3G_TROOPER", {{anim::kAnimIdle, 30u}, {anim::kAnimIdleCrouch, 30u}, {anim::kAnimMove, 20u},
                                                   {anim::kAnimCrouchMove, 20u}});
    SimAnimation animation(d->types, *d->files);
    animation.set_model(trooper, objects::ModeKind::Land, model.get());
    int phase = 0;
    objects::ObjectId unit = objects::kNoObjectId;
    uint32_t on_frame = 0, off_frame = 0;
    int32_t crouched = -1, standing = -1, standing_type = -1, last_type = -2;
    bool other = false;
    std::string trace;
    land_battle(*d, &animation, 900, [&](TacticalBattle& b, TacticalMode& tm, SimHost&) {
        objects::ObjectStore& st = tm.primary();
        const anim::AnimWorld* w = animation.find_world(tm);
        if (phase == 0) {
            if (b.frame() < 30) return true;
            for (const objects::ObjectHandle h : st.all_objects())
                if (unit == objects::kNoObjectId && st.alive(h) && st.type(h) == trooper && w && w->contains(st.id(h)))
                    unit = st.id(h);
            CHECK(unit != objects::kNoObjectId);
            if (unit == objects::kNoObjectId) return false;
            const objects::ObjectId ids[] = {unit};
            CHECK(alamo::sim::TacticalBattleTestAccess::abilities(b).set_unit_ability_mode(ids, tactical::abilities::kSpreadOut, true, b.frame()) == 1);
            CHECK(alamo::sim::TacticalBattleTestAccess::abilities(b).spread_out(unit));
            on_frame = b.frame();
            phase = 1;
            return true;
        }
        if (!w || !w->contains(unit) || !b.combat().alive(unit)) return false;
        const int32_t type = w->current_type(unit);
        if (type != last_type) {
            trace += " " + std::to_string(b.frame()) + ":" + std::to_string(type);
            last_type = type;
        }
        const bool crouch_clip = type == anim::kAnimIdleCrouch || type == anim::kAnimCrouchMove;
        if (phase == 1) {
            if (crouched < 0 && crouch_clip) crouched = static_cast<int32_t>(b.frame() - on_frame);
            if (type != anim::kAnimIdle && type != anim::kAnimMove && !crouch_clip) other = true;  // only the four clips
            if (type == anim::kAnimIdleCrouch) {  // stopped and crouching: SPREAD_OUT off
                const objects::ObjectId ids[] = {unit};
                CHECK(alamo::sim::TacticalBattleTestAccess::abilities(b).set_unit_ability_mode(ids, tactical::abilities::kSpreadOut, false, b.frame()) == 1);
                CHECK(!alamo::sim::TacticalBattleTestAccess::abilities(b).spread_out(unit));
                off_frame = b.frame();
                phase = 2;
            }
        } else if (phase == 2 && (type == anim::kAnimIdle || type == anim::kAnimMove)) {
            standing = static_cast<int32_t>(b.frame() - off_frame);
            standing_type = type;
            return false;
        }
        return b.frame() < on_frame + 600;
    });
    // On: the next locomotor service plays a crouch clip (CROUCHMOVE while moving, Idle Crouch once stopped); off: the
    // crouch change forces the plain idle at the next IDLE service.
    std::printf("  r3g crouch: crouch clip %d frames after SPREAD_OUT on (stopped crouching at +%d), standing clip %d frames "
                "after off; clip types (frame:type)%s\n",
                crouched, static_cast<int>(off_frame - on_frame), standing, trace.c_str());
    // mov1 (2026-11-17: the retail land tick order and the kept StoppedVsStopped record): the trooper can be set walking by a
    // collision resolution right when SPREAD_OUT goes off - then no IDLE service runs (it counts as moving) and the plain
    // clip is MOVE once the walk starts (measured +12); standing still, the plain idle comes at the next IDLE service
    CHECK(!other && crouched >= 0 && crouched <= 2 && standing >= 0 && (standing <= 2 || standing_type == anim::kAnimMove));
#else
    std::printf("  r3g crouch: skipped (no engine/anim)\n");
#endif
}

// ---- mcx4 tac2: reinforcement arrival rules ----
// Drops player 1's S10_Squad reserve at the first valid cell; true when the order went through.
bool drop_reserve(TacticalBattle& b, TacticalMode& tm, SimHost& host) {
    namespace mv = alamo::tactical::movement;
    const auto reserves = b.reserves(1);
    if (reserves.empty()) return false;
    const objects::ObjectId key = reserves[0].galactic_id;
    if (b.fog()) alamo::sim::TacticalBattleTestAccess::fog(b)->reveal_all(1);
    for (int32_t cy = 6; cy < 58; cy += 2)
        for (int32_t cx = 6; cx < 58; cx += 2) {
            const mv::V2 c = mv::cell_center(cx, cy);
            if (!b.land_drop_valid(tm, 1, key, c.x, c.y, false, false)) continue;
            if (b.order_reinforce(tm, 1, key, c.x, c.y, host.random().synced()) == commands::RejectReason::None) return true;
        }
    return false;
}

struct UnloadRun {
    bool dropped = false;
    int team0 = -1;                     // frames from the order to the first team step
    uint32_t step_frame = 0;            // battle frame of that step
    size_t members = 0, with_entry = 0;  // first step's combat members / those with the Elevated_Vulnerability entry
    float value = 0.0f;                 // the entry's value
    uint32_t expiry = 0;                // its expiry frame
    float defense_after = 1.0f;         // a member's Defense list 100 frames after the step
};

// FoC's values of the four Elevated_Vulnerability constants (the fixture's GameConstants set none otherwise).
constexpr const char* kFocVulnerability =
    " <Elevated_Vulnerability_Duration>3.0</Elevated_Vulnerability_Duration>\n"
    " <Elevated_Vulnerability_Factor>-2.0</Elevated_Vulnerability_Factor>\n"
    " <Space_Elevated_Vulnerability_Duration>5.0</Space_Elevated_Vulnerability_Duration>\n"
    " <Space_Elevated_Vulnerability_Factor>-3.0</Space_Elevated_Vulnerability_Factor>\n"
    " <Override_Death_Persistence_Duration>1.0</Override_Death_Persistence_Duration>\n";

UnloadRun run_unload(Data& d, bool skirmish) {
    UnloadRun out;
    uint32_t drop = 0;
    objects::ObjectId max_before = 0;
    std::vector<objects::ObjectId> members;
    static const uint32_t kElevated = name_crc("Elevated_Vulnerability");
    land_battle(
        d, nullptr, 1500,
        [&](TacticalBattle& b, TacticalMode& tm, SimHost& host) {
            objects::ObjectStore& st = tm.primary();
            auto& cs = const_cast<tactical::combat::CombatSystem&>(b.combat());
            if (!out.dropped) {
                if (b.frame() != 1) return true;
                for (const objects::ObjectHandle h : st.all_objects()) max_before = std::max(max_before, st.id(h));
                out.dropped = drop_reserve(b, tm, host);
                drop = b.frame();
                return out.dropped;
            }
            const int since = static_cast<int>(b.frame() - drop);
            if (out.team0 < 0) {
                for (const objects::ObjectHandle h : st.owned_by(1))
                    if (st.alive(h) && st.id(h) > max_before && cs.has_unit(st.id(h))) members.push_back(st.id(h));
                if (members.empty()) return since < 600;
                out.team0 = since;
                out.step_frame = b.frame();
                out.members = members.size();
                for (const objects::ObjectId id : members)
                    if (const tactical::combat::ModifierSet* m = cs.unit_modifiers(id))
                        for (const tactical::combat::ModifierEntry& e : m->entries())
                            if (e.source_crc == kElevated && e.list == static_cast<uint8_t>(tactical::combat::ModList::Defense)) {
                                ++out.with_entry;
                                out.value = e.value;
                                out.expiry = e.expiry;
                            }
                return true;
            }
            if (b.frame() < out.step_frame + 100) return true;
            if (cs.has_unit(members.front())) out.defense_after = cs.modifier(members.front(), tactical::combat::ModList::Defense);
            return false;
        },
        skirmish);
    return out;
}

// A Battlefield_Modifier on the trooper (the S10_Squad member): Disable_Reinforcement_Vulnerability for its owner's allies.
std::string disable_vulnerability_patch(std::string t) {
    const std::string at = "<GroundInfantry Name=\"S10_Trooper\">";
    if (const size_t p = t.find(at); p != std::string::npos)
        t.insert(p + at.size(), "<Abilities SubObjectList=\"Yes\"><Battlefield_Modifier_Ability Name=\"T_Enhanced_Deployment\">"
                                "<Activation_Style>Ground_Automatic</Activation_Style>"
                                "<Disable_Reinforcement_Vulnerability>True</Disable_Reinforcement_Vulnerability>"
                                "<Apply_To_All_Allies>Yes</Apply_To_All_Allies></Battlefield_Modifier_Ability></Abilities>");
    return t;
}

// MCX4_T10_RE_ANSWERS §16 / §16a / §15 [H] (Apply_Elevated_Vulnerability 0x3a8550, transport unload step 0x644330):
// each member of a landing's team step gets one Defense entry "Elevated_Vulnerability" with the factor for the duration:
// with FoC's Elevated_Vulnerability_Duration 3 s / Elevated_Vulnerability_Factor -2.0 (3x damage taken) the entry expires
// int(30 x 3 + 0.5) = 90 frames after the step and 100 frames later the Defense list is back to 0. With a
// Battlefield_Modifier Disable_Reinforcement_Vulnerability applied for the landing owner (here the team members' own
// Ground_Automatic ability, Apply_To_All_Allies), the team step adds no entry (the gate 0x527f00).
// ledger: verifies GameConstants.Elevated_Vulnerability_Duration
// ledger: verifies GameConstants.Elevated_Vulnerability_Factor
// ledger: verifies Battlefield_Modifier_Ability.Disable_Reinforcement_Vulnerability
void test_elevated_vulnerability() {
    g_temp_prefix = "alamo_sim_r3g_tests_mcx4_vuln";
    g_constants_extra = kFocVulnerability;
    auto d = make_data(false, false, false, false, false, false, false, false, false, false, false, true);
    const UnloadRun a = run_unload(*d, false);
    std::printf("  mcx4 elevated vulnerability: dropped %d, team step +%d, %zu member(s), %zu with the entry (value %.2f, "
                "expiry %u = step %u + %u), Defense 100 frames later %.2f\n",
                a.dropped, a.team0, a.members, a.with_entry, static_cast<double>(a.value), a.expiry, a.step_frame,
                a.expiry - a.step_frame, static_cast<double>(a.defense_after));
    CHECK(a.dropped && a.members > 0 && a.with_entry == a.members);
    CHECK(a.value == -2.0f && a.expiry >= a.step_frame + 89 && a.expiry <= a.step_frame + 90);
    CHECK(a.defense_after == 0.0f);
    g_temp_prefix = "alamo_sim_r3g_tests_mcx4_novuln";
    g_types_patch = disable_vulnerability_patch;
    auto g = make_data(false, false, false, false, false, false, false, false, false, false, false, true);
    g_types_patch = nullptr;
    g_constants_extra = nullptr;
    const UnloadRun n = run_unload(*g, false);
    std::printf("  mcx4 Disable_Reinforcement_Vulnerability: team step +%d, %zu member(s), %zu with the entry\n", n.team0,
                n.members, n.with_entry);
    CHECK(n.dropped && n.members > 0 && n.with_entry == 0);
}

// MCX4_T10_RE_ANSWERS §19, A1_READINGS §35.2 state 4 (clearance 0x642ef0) [H]: in a skirmish a timed landing waits at least
// Skirmish_Reinforcement_Delay_Frames (ctor and FoC 90; the fixture sets none) after its attach before the footprint test,
// so its first team step comes 89 frames later than in a campaign battle (clearance at +90 instead of +1).
// ledger: verifies GameConstants.Skirmish_Reinforcement_Delay_Frames
void test_skirmish_landing_delay() {
    g_temp_prefix = "alamo_sim_r3g_tests_mcx4_delay";
    auto d = make_data(false, false, false, false, false, false, false, false, false, false, false, true);
    const UnloadRun c = run_unload(*d, false);
    const UnloadRun s = run_unload(*d, true);
    std::printf("  mcx4 skirmish landing delay: campaign team step +%d, skirmish +%d\n", c.team0, s.team0);
    CHECK(c.dropped && s.dropped && c.team0 > 0 && s.team0 == c.team0 + 89);
}

// MCX4_T10_RE_ANSWERS §16 / §18 (0x29f270) [H]: the death clone of a dying Infantry type persists for
// Override_Death_Persistence_Duration, not its own Death_Persistence_Duration. The crushed trooper's S10_Trooper_Crush_Clone
// (5 s of its own, primary clone object) goes after the override (1 s here) plus the default Death_Fade_Time 0.25 s:
// int(30 x 1) + int(30 x 0.25) = 37 frames, not 157.
// ledger: verifies GameConstants.Override_Death_Persistence_Duration
std::string persist_patch(std::string t) {
    // crush_patch with the clones' HIDE_WHEN_FOGGED in the generic <Behavior> set, so they keep the primary path (a clone
    // object the battle removes at its planned frame; death_clone_corpse_path reads the generic set only).
    t = crush_patch(std::move(t));
    for (size_t p = t.find("<LandBehavior>HIDE_WHEN_FOGGED</LandBehavior>"); p != std::string::npos;
         p = t.find("<LandBehavior>HIDE_WHEN_FOGGED</LandBehavior>", p))
        t.replace(p, 45, "<Behavior>HIDE_WHEN_FOGGED</Behavior>");
    return t;
}

void test_death_persistence_override() {
    g_temp_prefix = "alamo_sim_r3g_tests_mcx4_persist";
    g_types_patch = persist_patch;
    g_constants_extra = kFocVulnerability;
    auto d = make_data();
    g_types_patch = nullptr;
    g_constants_extra = nullptr;
    const objects::TypeIndex trooper = d->types.resolve_name("S10_Trooper");
    objects::ObjectId victim = objects::kNoObjectId, crusher = objects::kNoObjectId;
    uint32_t crushed = 0;
    int gone = -1;
    size_t pending = 0;
    land_battle(*d, nullptr, 600, [&](TacticalBattle& b, TacticalMode& tm, SimHost&) {
        objects::ObjectStore& st = tm.primary();
        if (b.frame() < 2) return true;
        if (crushed == 0) {
            for (const objects::ObjectHandle h : st.all_objects()) {
                if (!st.alive(h) || !b.combat().has_unit(st.id(h)) || !b.combat().alive(st.id(h))) continue;
                if (victim == objects::kNoObjectId && st.type(h) == trooper) victim = st.id(h);
                else if (crusher == objects::kNoObjectId) crusher = st.id(h);
            }
            const bool ok = victim != objects::kNoObjectId && crusher != objects::kNoObjectId && alamo::sim::TacticalBattleTestAccess::crush(b, crusher, victim);
            if (!ok) std::printf("  mcx4 death persistence override: victim %d crusher %d, crush refused\n", victim, crusher);
            if (!ok) return false;
            crushed = b.frame();
            return true;
        }
        if (pending == 0) {  // the clone is made when the death is processed (this or a later tick)
            pending = b.death_clones_pending();
            if (pending > 0) crushed = b.frame();
        } else if (b.death_clones_pending() < pending) {
            gone = static_cast<int>(b.frame() - crushed);
            return false;
        }
        return b.frame() < crushed + 300;
    });
    std::printf("  mcx4 death persistence override: clone pending %zu after the crush, removed +%d\n", pending, gone);
    CHECK(pending > 0 && gone >= 36 && gone <= 39);
}

// MCX4_T10_RE_ANSWERS §16a addendum [H]: the space reinforcement drop 0x2bf2d0 ends with Apply_Elevated_Vulnerability on
// every arriving unit (no gate), with the space pair: FoC Space_Elevated_Vulnerability_Duration 5 s /
// Space_Elevated_Vulnerability_Factor -3.0 (4x damage taken), so the entry expires 150 frames after the drop. Player 1's
// extra corvettes wait in the pool (unit cap); one of its fleet units is removed first to make room.
// ledger: verifies GameConstants.Space_Elevated_Vulnerability_Duration
// ledger: verifies GameConstants.Space_Elevated_Vulnerability_Factor
void test_space_reinforcement_vulnerability() {
    g_temp_prefix = "alamo_sim_r3g_tests_mcx4_space";
    g_constants_extra = kFocVulnerability;
    auto d = make_data();
    g_constants_extra = nullptr;
    jobs::JobSystem js(jobs::JobSystemConfig{0});
    SimConfig cfg;
    cfg.seed = 2026;
    cfg.players = 4;
    SimHost host(d->types, js, cfg);
    galactic::GalacticSetup setup = make_setup(*d, false);
    for (int i = 0; i < 3; ++i) setup.forces.push_back({1, 2, type_of(*d, "S10_Corvette")});
    galactic::GalacticSim* gs = host.start_galactic(d->rules, std::move(setup));
    CHECK(gs != nullptr);
    if (!gs) return;
    CampaignBattleOptions opt;
    opt.conflict.human_choice = galactic::BattleChoice::Fight;
    opt.battle.max_frames = 600;
    opt.battle.order_interval = 0;  // no stand-in orders
    CampaignBattles battles(host, d->crules, *d->catalog,
                            [](const std::string& name, terrain::MapData& m) {
                                if (name != "S10_Space_B") return false;
                                m = space_map();
                                return true;
                            },
                            opt);
    CHECK(battles.attach(nullptr));
    const std::vector<uint32_t> uf = gs->fleets_at(2, 1);
    if (!uf.empty()) CHECK(gs->move_fleet(1, uf[0], 1) == commands::RejectReason::None);
    static const uint32_t kElevated = name_crc("Elevated_Vulnerability");
    bool killed = false, dropped = false;
    uint32_t drop_frame = 0;
    objects::ObjectId max_before = 0;
    size_t units = 0, with_entry = 0;
    float value = 0.0f;
    uint32_t expiry = 0;
    for (uint64_t t = 0; t < 3000 && !dropped; ++t) {
        host.tick_once();
        TacticalBattle* b = battles.battle();
        TacticalMode* tm = host.tactical();
        if (!b || !tm || b->frame() < 1 || b->finished()) continue;
        objects::ObjectStore& st = tm->primary();
        auto& cs = const_cast<tactical::combat::CombatSystem&>(b->combat());
        if (!killed) {
            if (b->fog()) alamo::sim::TacticalBattleTestAccess::fog(*b)->reveal_all(1);  // the space drop validity refuses a fogged point
            for (const objects::ObjectHandle h : st.owned_by(1))
                if (st.alive(h) && cs.has_unit(st.id(h)) && cs.alive(st.id(h))) {
                    killed = b->script_damage(*tm, st.id(h), 1.0e9f, false);
                    break;
                }
            continue;
        }
        const auto r = b->reserves(1);
        if (r.empty()) {
            std::printf("  mcx4 space reinforcement: no reserve of player 1 at frame %u\n", b->frame());
            break;
        }
        max_before = 0;
        for (const objects::ObjectHandle h : st.all_objects()) max_before = std::max(max_before, st.id(h));
        if (const commands::RejectReason rr = b->order_reinforce(*tm, 1, r[0].galactic_id, 0.0f, 0.0f, host.random().synced());
            rr != commands::RejectReason::None) {
            if (b->frame() % 100 == 0) std::printf("  mcx4 space reinforcement: frame %u refused %d\n", b->frame(), static_cast<int>(rr));
            continue;
        }
        dropped = true;
        drop_frame = b->frame();
        for (const objects::ObjectHandle h : st.owned_by(1)) {
            if (!st.alive(h) || st.id(h) <= max_before || !cs.has_unit(st.id(h))) continue;
            ++units;
            if (const tactical::combat::ModifierSet* m = cs.unit_modifiers(st.id(h)))
                for (const tactical::combat::ModifierEntry& e : m->entries())
                    if (e.source_crc == kElevated && e.list == static_cast<uint8_t>(tactical::combat::ModList::Defense)) {
                        ++with_entry;
                        value = e.value;
                        expiry = e.expiry;
                    }
        }
    }
    std::printf("  mcx4 space reinforcement: killed %d, dropped %d at %u, %zu unit(s), %zu with the entry (value %.2f, expiry +%u)\n",
                killed, dropped, drop_frame, units, with_entry, static_cast<double>(value), expiry - drop_frame);
    CHECK(killed && dropped && units > 0 && with_entry == units);
    CHECK(value == -3.0f && expiry >= drop_frame + 149 && expiry <= drop_frame + 150);
}

// MCX4_T09_RE_ANSWERS Q10 [H]: Income_Stream_Ability / Income_Stream_Mod_Ability as parse_income_stream reads them (the
// validator 0x703d00 / 0x704880 and the create 0x703990), the fold 0x33fb70, the per-frame pay 0x6951a0 with the effective
// interval 0x694b30, the share modes 0x694ba0 / 0x694e90 and the mod filter of apply 0x703fe0.
// ledger: verifies Income_Stream_Ability.Split_Favors_Owner
// ledger: verifies Income_Stream_Ability.Full_Amount_To_Everyone
// ledger: verifies Income_Stream_Ability.Owner_Income_Percentage
// ledger: verifies Income_Stream_Mod_Ability.Interval_Multiplier
// ledger: verifies Income_Stream_Mod_Ability.Affects_All_Allied_Sources
std::string income_patch(std::string t) {
    const size_t end = t.find("</S10>");
    if (end != std::string::npos)
        t.insert(end,
                 // favours the owner with a percentage >= 1 (clamped to 0.99); a mod on itself: m 2, im <= 0 (-> 1.0)
                 " <SpaceUnit Name=\"S10_Inc_Favor\"><Abilities SubObjectList=\"Yes\"><Income_Stream_Ability Name=\"I1\">"
                 "<Base_Income_Value>50</Base_Income_Value><Base_Interval_In_Secs>10</Base_Interval_In_Secs>"
                 "<Split_Income_With_Allies>Yes</Split_Income_With_Allies><Split_Favors_Owner>Yes</Split_Favors_Owner>"
                 "<Owner_Income_Percentage>1.5</Owner_Income_Percentage></Income_Stream_Ability>"
                 "<Income_Stream_Mod_Ability Name=\"M1\"><Income_Multiplier>2.0</Income_Multiplier>"
                 "<Interval_Multiplier>0</Interval_Multiplier><Stacking_Category>3</Stacking_Category>"
                 "<Target_Stream_Source>S10_Inc_Favor</Target_Stream_Source>"
                 "<Affects_All_Allied_Sources>Yes</Affects_All_Allied_Sources></Income_Stream_Mod_Ability></Abilities></SpaceUnit>\n"
                 // both share flags: the validator clears both (mode 0); an interval mod 0.5 on another source, own only
                 " <SpaceUnit Name=\"S10_Inc_Both\"><Abilities SubObjectList=\"Yes\"><Income_Stream_Ability Name=\"I2\">"
                 "<Base_Income_Value>30</Base_Income_Value><Base_Interval_In_Secs>5</Base_Interval_In_Secs>"
                 "<Split_Income_With_Allies>Yes</Split_Income_With_Allies><Split_Favors_Owner>Yes</Split_Favors_Owner>"
                 "<Full_Amount_To_Everyone>Yes</Full_Amount_To_Everyone></Income_Stream_Ability>"
                 "<Income_Stream_Mod_Ability Name=\"M2\"><Interval_Multiplier>0.5</Interval_Multiplier>"
                 "<Target_Stream_Source>S10_Inc_Favor</Target_Stream_Source>"
                 "<Affects_All_Allied_Sources>No</Affects_All_Allied_Sources></Income_Stream_Mod_Ability></Abilities></SpaceUnit>\n"
                 // full amount to everyone; favours the owner without split (cleared, percentage 0)
                 " <SpaceUnit Name=\"S10_Inc_Full\"><Abilities SubObjectList=\"Yes\"><Income_Stream_Ability Name=\"I3\">"
                 "<Base_Income_Value>10</Base_Income_Value><Base_Interval_In_Secs>1</Base_Interval_In_Secs>"
                 "<Split_Income_With_Allies>Yes</Split_Income_With_Allies><Full_Amount_To_Everyone>Yes</Full_Amount_To_Everyone>"
                 "</Income_Stream_Ability></Abilities></SpaceUnit>\n"
                 " <SpaceUnit Name=\"S10_Inc_Solo\"><Abilities SubObjectList=\"Yes\"><Income_Stream_Ability Name=\"I4\">"
                 "<Base_Income_Value>10</Base_Income_Value><Base_Interval_In_Secs>1</Base_Interval_In_Secs>"
                 "<Split_Favors_Owner>Yes</Split_Favors_Owner><Owner_Income_Percentage>0.5</Owner_Income_Percentage>"
                 "</Income_Stream_Ability></Abilities></SpaceUnit>\n");
    return t;
}

void test_income_streams() {
    g_temp_prefix = "alamo_sim_r3g_tests_mcx4_income";
    g_types_patch = income_patch;
    auto d = make_data();
    g_types_patch = nullptr;
    const auto ty = [&](const char* n) { return static_cast<uint32_t>(d->types.resolve_name(n)); };
    const uint32_t favor_t = ty("S10_Inc_Favor");
    const IncomeStreamDef favor = parse_income_stream(d->types, favor_t), both = parse_income_stream(d->types, ty("S10_Inc_Both"));
    const IncomeStreamDef full = parse_income_stream(d->types, ty("S10_Inc_Full")), solo = parse_income_stream(d->types, ty("S10_Inc_Solo"));
    std::printf("  mcx4 income parse: favor split %d mode %u share %.2f mods %zu (m %.1f im %.1f cat %d all %d); both mode %u; "
                "full mode %u; solo mode %u share %.2f\n",
                favor.split, favor.mode, static_cast<double>(favor.owner_share), favor.mods.size(),
                favor.mods.empty() ? 0.0 : static_cast<double>(favor.mods[0].multiplier),
                favor.mods.empty() ? 0.0 : static_cast<double>(favor.mods[0].interval), favor.mods.empty() ? -1 : favor.mods[0].category,
                favor.mods.empty() ? 0 : favor.mods[0].all_allied, both.mode, full.mode, solo.mode, static_cast<double>(solo.owner_share));
    CHECK(favor.value == 50.0f && favor.interval == 10.0f && favor.split && favor.mode == 1 && favor.owner_share == 0.99f);
    CHECK(favor.mods.size() == 1 && favor.mods[0].target_type == favor_t && favor.mods[0].multiplier == 2.0f &&
          favor.mods[0].interval == 1.0f && favor.mods[0].category == 3 && favor.mods[0].all_allied);
    CHECK(both.mode == 0 && both.split && both.mods.size() == 1 && both.mods[0].interval == 0.5f && !both.mods[0].all_allied);
    CHECK(full.mode == 2 && full.split);
    CHECK(solo.mode == 0 && !solo.split && solo.owner_share == 0.0f);
    // Fold: per category the largest value, the maxima summed (0.2 / 0.4 in category 1 -> 0.4, plus 1.0 in category 0).
    const IncomeModEntry e1[] = {{1, 0.2f, 0.0f, 0.0f}, {1, 0.4f, 0.0f, 0.0f}, {0, 1.0f, 20.0f, 0.0f}};
    CHECK(income_fold(e1, 0) == 1.0f + 0.4f && income_fold(e1, 1) == 20.0f && income_fold(e1, 2) == 0.0f);
    // Pay: ((M + 1) x base + A) / I / FPS.
    CHECK(income_stream_pay(50.0f, 10.0f, {}) == 50.0f / 10.0f / 30.0f);
    CHECK(income_stream_pay(50.0f, 10.0f, e1) == ((1.4f + 1.0f) * 50.0f + 20.0f) / 10.0f / 30.0f);
    // Interval_Multiplier: im - 1 in the interval map; 0.5 halves the interval, 2.0 doubles it, the floor is 1 s; two cuts
    // in one category: the larger im - 1 (the smaller cut) wins.
    const IncomeModEntry half[] = {{0, 0.0f, 0.0f, -0.5f}}, twice[] = {{0, 0.0f, 0.0f, 1.0f}}, deep[] = {{0, 0.0f, 0.0f, -0.95f}};
    const IncomeModEntry two_cuts[] = {{2, 0.0f, 0.0f, -0.5f}, {2, 0.0f, 0.0f, -0.25f}};
    CHECK(income_stream_pay(50.0f, 10.0f, half) == 50.0f / 5.0f / 30.0f);
    CHECK(income_stream_pay(50.0f, 10.0f, twice) == 50.0f / 20.0f / 30.0f);
    CHECK(income_stream_pay(50.0f, 10.0f, deep) == 50.0f / 1.0f / 30.0f);
    CHECK(income_stream_pay(50.0f, 10.0f, two_cuts) == 50.0f / 7.5f / 30.0f);
    // Shares of a 0.3 pay among recipients {0, 1, 2} of owner 0.
    const float pay = 0.3f;
    CHECK(income_share(2, 0.0f, 0, 1, 3, pay) == pay);                                  // Full_Amount_To_Everyone
    CHECK(income_share(1, 0.5f, 0, 0, 3, pay) == pay * 0.5f);                           // Split_Favors_Owner: owner s
    CHECK(income_share(1, 0.5f, 0, 2, 3, pay) == (pay - pay * 0.5f) / 3.0f);            // others (pay - s) / N, N with the owner
    CHECK(income_share(1, 0.99f, 0, 0, 3, pay) == pay * 0.99f);                         // Owner_Income_Percentage
    CHECK(income_share(1, 1.0f, 0, 1, 3, pay) == 0.0f);                                 // a share <= 0 is not paid
    CHECK(income_share(0, 0.0f, 0, 1, 3, pay) == pay / 3.0f);                           // equal split
    // Affects_All_Allied_Sources: without it only the stream owner's own mods; with it an ally's mod too, but only on a
    // stream that lists the mod owner as a recipient.
    const int32_t split_r[] = {0, 1}, solo_r[] = {0};
    IncomeModDef m;
    m.target_type = favor_t;
    CHECK(income_mod_applies(m, favor_t, 0, 0, solo_r) && !income_mod_applies(m, favor_t, 0, 1, split_r));
    CHECK(!income_mod_applies(m, favor_t + 1u, 0, 0, solo_r));
    m.all_allied = true;
    CHECK(income_mod_applies(m, favor_t, 0, 1, split_r) && !income_mod_applies(m, favor_t, 0, 1, solo_r));
}

// ---- #44 t01: the animation-timed abilities in a land battle ----
// R3J_Walker: a DEPLOY_TROOPERS walker (S10_Squad, bones B_Trooper_00 / _01) with a skeleton model (bones 20 above its
// origin, 10 to either side) and a Damage_Cable_Attack death clone (CA_DIE); R3J_Speeder: a FLYING_LOCOMOTOR speeder with
// the cable (FoC Snowspeeder's phases, range 10 - 1000). The tests give the defender (player 0) one each on S10_B.
std::string r3j_troopers_patch(std::string t) {
    const size_t end = t.find("</S10>");
    if (end == std::string::npos) return t;
    t.insert(end,
             " <GroundVehicle Name=\"R3J_Walker\"><Tactical_Health>100000</Tactical_Health><Max_Speed>1.0</Max_Speed>"
             "<AI_Combat_Power>100</AI_Combat_Power><CategoryMask>Vehicle</CategoryMask><MovementClass>Tracked</MovementClass>"
             "<Behavior>SELECTABLE</Behavior><Collidable_By_Projectile_Living>Yes</Collidable_By_Projectile_Living>"
             "<Land_Model_Name>R3J_Walker.alo</Land_Model_Name>"
             "<LandBehavior>ABILITY_COUNTDOWN, DEPLOY_TROOPERS, WALK_LOCOMOTOR</LandBehavior>"
             "<Custom_Soft_Footprint_Radius>10</Custom_Soft_Footprint_Radius>"
             "<Custom_Hard_XExtent>10</Custom_Hard_XExtent><Custom_Hard_YExtent>10</Custom_Hard_YExtent>"
             "<Unit_Abilities_Data SubObjectList=\"Yes\"><Unit_Ability><Type>DEPLOY_TROOPERS</Type>"
             "<Recharge_Seconds>20</Recharge_Seconds><Spawned_Object_Type>S10_Squad</Spawned_Object_Type>"
             "<Max_Num_Spawned_Objects>2</Max_Num_Spawned_Objects>"
             "<Owner_Attachment_Bone>B_Trooper_00, B_Trooper_01</Owner_Attachment_Bone></Unit_Ability></Unit_Abilities_Data>"
             "<Death_Clone>Damage_Cable_Attack, R3J_Walker_CA_Clone</Death_Clone></GroundVehicle>\n"
             " <GroundVehicle Name=\"R3J_Walker_CA_Clone\"><Variant_Of_Existing_Type>R3J_Walker</Variant_Of_Existing_Type>"
             "<Specific_Death_Anim_Type>CA_DIE</Specific_Death_Anim_Type><Death_Persistence_Duration>5</Death_Persistence_Duration>"
             "</GroundVehicle>\n"
             " <GroundVehicle Name=\"R3J_Speeder\"><Tactical_Health>5000</Tactical_Health><Max_Speed>3.0</Max_Speed>"
             "<AI_Combat_Power>50</AI_Combat_Power><CategoryMask>Air</CategoryMask><MovementClass>Speeder</MovementClass>"
             "<Behavior>SELECTABLE</Behavior><LandBehavior>ABILITY_COUNTDOWN, FLYING_LOCOMOTOR</LandBehavior>"
             "<Custom_Soft_Footprint_Radius>6</Custom_Soft_Footprint_Radius>"
             "<Custom_Hard_XExtent>6</Custom_Hard_XExtent><Custom_Hard_YExtent>6</Custom_Hard_YExtent>"
             "<Unit_Abilities_Data SubObjectList=\"Yes\"><Unit_Ability><Type>CABLE_ATTACK</Type><Recharge_Seconds>120</Recharge_Seconds>"
             "<GUI_Activated_Ability_Name>R3J_Cable</GUI_Activated_Ability_Name></Unit_Ability></Unit_Abilities_Data>"
             "<Abilities SubObjectList=\"Yes\"><Cable_Ability Name=\"R3J_Cable\"><Activation_Style>User_Input</Activation_Style>"
             "<Activation_Min_Range>10.0</Activation_Min_Range><Activation_Max_Range>1000.0</Activation_Max_Range>"
             "<Activation_Chance>1.0</Activation_Chance><Drop_Phase_Duration>110</Drop_Phase_Duration>"
             "<Pull_Phase_Duration>140</Pull_Phase_Duration><Paralyze_Phase_Duration>300</Paralyze_Phase_Duration>"
             "<Applicable_Unit_Types>R3J_Walker</Applicable_Unit_Types></Cable_Ability></Abilities></GroundVehicle>\n"
             " <GroundCompany Name=\"R3J_Speeder_Co\"><Company_Units>R3J_Speeder</Company_Units>"
             "<Population_Value>1</Population_Value></GroundCompany>\n"
             " <GroundCompany Name=\"R3J_Walker_Co\"><Company_Units>R3J_Walker</Company_Units>"
             "<Population_Value>1</Population_Value></GroundCompany>\n");
    return t;
}

void r3j_troopers_models(const fs::path& root) {
    const std::vector<uint8_t> alo = skeleton_alo({{"Root", {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0}},
                                                   {"B_Trooper_00", {1, 0, 0, 10.0f, 0, 1, 0, 0, 0, 0, 1, 20.0f}},
                                                   {"B_Trooper_01", {1, 0, 0, -10.0f, 0, 1, 0, 0, 0, 0, 1, 20.0f}}});
    std::error_code ec;
    fs::create_directories(root / "Data/Art/Models", ec);
    std::ofstream f(root / "Data/Art/Models/R3J_Walker.alo", std::ios::binary | std::ios::trunc);
    f.write(reinterpret_cast<const char*>(alo.data()), static_cast<std::streamsize>(alo.size()));
}

// #44 t01 (REMAINING_GAPS §5.2, R3G_ANIM_TIMED_RE §2): DEPLOY_TROOPERS through the battle host. The walker stands, so it
// deploys at once; with the synthetic clips (Drop Rope 31, Deploy Troopers 16, Lift Rope 31 frames on the walker, Rope
// Land 13 on the troopers, 30 fps) the two troopers spawn at their bones hanging from the walker (attached_to = the walker,
// attach_bone = their bone index), locked by the rope (Immobilised once, which the host turns into mv_first_lock: adopted,
// coordinator hold NoFormation), are released 14 frames later (TrooperReleased clears both) and the ability ends 29
// frames after that, as in the ability system's own timing (test_r3g_deploy_troopers_timed: 30 / 44 / 73).
// #57 t02: the clips play on the battle's animation world - the walker shows Drop Rope from the order (it stands, so it is
// deployed at once), Deploy Troopers from the spawn and Lift Rope from the release; each trooper shows Rope Slide looping
// from its spawn and Rope Land (one-shot) after the release, once its slide has ended.
void test_deploy_troopers_battle() {
#ifdef ALAMO_SIM_HAVE_ANIM
    g_temp_prefix = "alamo_sim_r3g_tests_r3j_troopers";
    g_types_patch = r3j_troopers_patch;
    g_data_extra = r3j_troopers_models;
    auto d = make_data();
    g_types_patch = nullptr;
    g_data_extra = nullptr;
    using TA = alamo::sim::TacticalBattleTestAccess;
    const objects::TypeIndex walker_t = d->types.resolve_name("R3J_Walker"), trooper_t = d->types.resolve_name("S10_Trooper");
    CHECK(walker_t != objects::kNone && trooper_t != objects::kNone);
    const uint32_t walker_co = type_of(*d, "R3J_Walker_Co");
    const auto walker_model = synth_model("R3J_WALKER", {{anim::kAnimIdle, 30u}, {0x61, 31u}, {0x5e, 16u}, {0x62, 31u}});
    const auto trooper_model = synth_model("R3J_TROOPER", {{anim::kAnimIdle, 30u}, {0x5f, 10u}, {0x60, 13u}});
    SimAnimation animation(d->types, *d->files);
    animation.set_model(walker_t, objects::ModeKind::Land, walker_model.get());
    animation.set_model(trooper_t, objects::ModeKind::Land, trooper_model.get());
    struct Seen {
        objects::ObjectId id = objects::kNoObjectId;
        int spawn = -1, released = -1;
        int32_t bone = -2;
        bool hung = false;
        int immobile = 0;              // the most Immobilised holds seen while it hangs (the rope lock)
        uint8_t hold_spawn = 99, hold_hung = 99;  // movement coordinator hold at the spawn and after a rope service
        int slide_loop = -1, land = -1;           // #57 t02: Rope Slide looping (1) a frame after the spawn; Rope Land's first frame
    };
    std::string walker_clips;  // #57 t02: "type@since" each time the walker's clip type changes
    int32_t walker_type = -1;
    objects::ObjectId walker = objects::kNoObjectId;
    uint32_t order = 0;
    std::vector<objects::ObjectId> before;
    std::vector<Seen> seen;
    int done = -1;
    land_battle(
        *d, &animation, 900,
        [&](TacticalBattle& b, TacticalMode& tm, SimHost&) {
            const objects::ObjectStore& st = tm.primary();
            if (walker == objects::kNoObjectId) {
                if (b.frame() < 2) return true;
                for (const objects::ObjectHandle h : st.all_objects()) {
                    if (!st.alive(h)) continue;
                    if (st.type(h) == walker_t && st.owner(h) == 0) walker = st.id(h);
                    if (st.type(h) == trooper_t) before.push_back(st.id(h));
                }
                CHECK(walker != objects::kNoObjectId);
                if (walker == objects::kNoObjectId) return false;
                const objects::ObjectId ids[] = {walker};
                CHECK(TA::abilities(b).set_unit_ability_mode(ids, tactical::abilities::kDeployTroopers, true, b.frame()) == 1);
                order = b.frame();
                return true;
            }
            const int since = static_cast<int>(b.frame() - order);
            const anim::AnimWorld* aw = animation.find_world(tm);
            const auto clip_of = [aw](objects::ObjectId id) { return aw && aw->contains(id) ? aw->slot(id) : anim::SlotView{}; };
            if (const anim::SlotView wv = clip_of(walker); wv.type != walker_type) {
                walker_type = wv.type;
                char t[32];
                std::snprintf(t, sizeof t, "%s%x@%d", walker_clips.empty() ? "" : " ", static_cast<unsigned>(wv.type), since);
                walker_clips += t;
            }
            for (const objects::ObjectHandle h : st.all_objects()) {
                if (!st.alive(h) || st.type(h) != trooper_t || st.owner(h) != 0) continue;
                const objects::ObjectId id = st.id(h);
                if (std::find(before.begin(), before.end(), id) != before.end()) continue;
                bool known = false;
                for (const Seen& x : seen) known = known || x.id == id;
                if (!known) {
                    Seen x;
                    x.id = id;
                    x.spawn = since;
                    x.hold_spawn = static_cast<uint8_t>(b.movement().coordinator_hold(id));
                    seen.push_back(x);
                }
            }
            for (Seen& x : seen) {
                const anim::SlotView tv = clip_of(x.id);
                if (x.slide_loop < 0 && since == x.spawn + 1) x.slide_loop = tv.type == 0x5f && tv.mode == anim::SlotMode::Loop ? 1 : 0;
                if (x.land < 0 && tv.type == 0x60 && tv.mode == anim::SlotMode::OneShot) x.land = since;
            }
            for (Seen& x : seen) {
                objects::ObjectId to = objects::kNoObjectId;
                int32_t bone = -1;
                if (x.released >= 0 || !TA::attachment(b, x.id, to, bone)) continue;
                if (to == walker && bone >= 0) {
                    x.hung = true;
                    x.bone = bone;
                    x.immobile = std::max(x.immobile, static_cast<int>(TA::abilities(b).immobilize_count(x.id)));
                    if (since > x.spawn) x.hold_hung = static_cast<uint8_t>(b.movement().coordinator_hold(x.id));
                } else if (x.hung && to == objects::kNoObjectId && bone == -1) {
                    x.released = since;
                }
            }
            if (done < 0 && !seen.empty() && !TA::abilities(b).is_active(walker, tactical::abilities::kDeployTroopers)) done = since;
            return done < 0 && since < 400;
        },
        false, [walker_co](galactic::GalacticSetup& s) { s.forces.push_back({0, 1, walker_co}); });
    std::printf("  #44 t01 deploy troopers: %zu troopers", seen.size());
    for (const Seen& x : seen)
        std::printf(" [bone %d spawn +%d released +%d, immobilised %d, hold %u -> %u]", x.bone, x.spawn, x.released, x.immobile,
                    static_cast<unsigned>(x.hold_spawn), static_cast<unsigned>(x.hold_hung));
    std::printf(", ability done +%d; walker clips %s; troopers' Rope Slide looping / Rope Land", done, walker_clips.c_str());
    for (const Seen& x : seen) std::printf(" %d / +%d", x.slide_loop, x.land);
    std::printf("\n");
    CHECK(seen.size() == 2);
    // #57 t02: Drop Rope 30 frames before the spawn (the ability test's 0 / 30 / 44), Deploy Troopers at the spawn, Lift
    // Rope at the release (the walker's clips before the order, if any, are not these)
    const std::string rope_clips = "61@2 5e@32 62@46";
    CHECK(walker_clips.size() >= rope_clips.size() && walker_clips.compare(walker_clips.size() - rope_clips.size(), rope_clips.size(), rope_clips) == 0);
    // Rope Land at the slide's end: the bones are 20 above the walker on flat ground, int(20 / 3.9) + 1 = 6 services
    for (const Seen& x : seen) CHECK(x.slide_loop == 1 && x.land == x.released + 6);
    if (seen.size() == 2) {
        CHECK(seen[0].hung && seen[1].hung && seen[0].bone + seen[1].bone == 1);
        for (const Seen& x : seen) CHECK(x.released - x.spawn == 14);
        // the rope lock (Immobilised, once) reaches mv_first_lock: the trooper, born without a coordinator hold, is adopted
        // (CoordinatorHold::NoFormation) while it hangs (cu8l t02, CU8K_T02_T06_RE §3 rule 6)
        for (const Seen& x : seen) CHECK(x.immobile == 1 && x.hold_spawn == 0 && x.hold_hung == 1);
        CHECK(done - seen[0].released == 29);
    }
#else
    std::printf("  #44 t01 deploy troopers: skipped (no engine/anim)\n");
#endif
}

// #44 t01 (R3G_ANIM_TIMED_RE §4, CableAttackAbility 0x6f2d50): a cable trip through the battle host. The defender's
// R3J_Speeder (FLYING_LOCOMOTOR, CABLE_ATTACK: drop 110, pull 140, paralyze 300) cables an R3J_Walker that is handed to
// the attacker and ordered across the map (the S10 stand-in orderer is off, so nothing else orders either unit); it is
// still walking when the wrapping passes 30 services (about 282 frames
// after the order, the ability system's own timing), so it takes a kind-8 (Damage_Cable_Attack) kill for its health and
// dies through its Damage_Cable_Attack death clone, R3J_Walker_CA_Clone (Specific_Death_Anim_Type CA_DIE).
// `stop` (#44 t03, 0x6f3180): the speeder is stopped that many frames after the order (Stop leaves FlyCircling); inside
// the drop / pull (< 250) that ends the cable: the walker is neither killed nor held (Immobilised) by +600. -1: never
// stopped.
void test_cable_trip_battle(int stop) {
    g_temp_prefix = "alamo_sim_r3g_tests_r3j_cable";
    g_types_patch = r3j_troopers_patch;
    g_data_extra = r3j_troopers_models;
    auto d = make_data();
    g_types_patch = nullptr;
    g_data_extra = nullptr;
    using TA = alamo::sim::TacticalBattleTestAccess;
    const objects::TypeIndex walker_t = d->types.resolve_name("R3J_Walker"), speeder_t = d->types.resolve_name("R3J_Speeder"),
                             clone_t = d->types.resolve_name("R3J_Walker_CA_Clone");
    CHECK(walker_t != objects::kNone && speeder_t != objects::kNone && clone_t != objects::kNone);
    CHECK(d->types.get_string(clone_t, "Specific_Death_Anim_Type").find("CA_DIE") != std::string_view::npos);
    const uint32_t walker_co = type_of(*d, "R3J_Walker_Co"), speeder_co = type_of(*d, "R3J_Speeder_Co");
    objects::ObjectId walker = objects::kNoObjectId, speeder = objects::kNoObjectId;
    uint32_t order = 0;
    int died = -1;
    uint8_t kind = 0;
    int32_t clone = -2;
    objects::ObjectId killer = objects::kNoObjectId;
    int held = 0;  // the most Immobilised holds on the walker
    land_battle(
        *d, nullptr, 900,
        [&](TacticalBattle& b, TacticalMode& tm, SimHost& host) {
            objects::ObjectStore& st = tm.primary();
            if (walker == objects::kNoObjectId) {
                if (b.frame() < 2) return true;
                objects::ObjectHandle wh{};
                for (const objects::ObjectHandle h : st.all_objects()) {
                    if (!st.alive(h) || st.owner(h) != 0) continue;
                    if (st.type(h) == walker_t) {
                        walker = st.id(h);
                        wh = h;
                    }
                    if (st.type(h) == speeder_t) speeder = st.id(h);
                }
                CHECK(walker != objects::kNoObjectId && speeder != objects::kNoObjectId);
                if (walker == objects::kNoObjectId || speeder == objects::kNoObjectId) return false;
                CHECK(b.change_owner(tm, wh, 1));
                const Vec3 p = st.position(wh);
                const objects::ObjectId walkers[] = {walker};
                const float to_x = p.x > 600.0f ? p.x - 700.0f : p.x + 700.0f;
                CHECK(b.order_move(tm, 1, walkers, to_x, p.y, host.random().synced()) == commands::RejectReason::None);
                const objects::ObjectId users[] = {speeder};
                CHECK(TA::abilities(b).targeted_ability(users, tactical::abilities::kCableAttack, walker, -1, b.frame()) == 1);
                order = b.frame();
                return true;
            }
            const int since = static_cast<int>(b.frame() - order);
            if (since == stop) {
                const objects::ObjectId users[] = {speeder};
                CHECK(b.order_stop(tm, 0, users) == commands::RejectReason::None);
            }
            held = std::max(held, static_cast<int>(TA::abilities(b).immobilize_count(walker)));
            for (const tactical::combat::DeathEvent& e : b.combat().deaths())
                if (e.id == walker && died < 0) {
                    died = since;
                    kind = e.kind;
                    clone = e.death_clone;
                    killer = e.killer;
                }
            return died < 0 && since < 600;
        },
        false,
        [walker_co, speeder_co](galactic::GalacticSetup& s) {
            s.forces.push_back({0, 1, walker_co});
            s.forces.push_back({0, 1, speeder_co});
        },
        // the S10 stand-in orderer ([port] TacticalBattle::issue_orders) would send both units at their nearest enemy: off
        [](CampaignBattleOptions& o) { o.battle.order_interval = 0; });
    std::printf("  #44 t01 cable (speeder stopped at %d): walker died +%d, kind %u, clone %d (CA clone %d), killer %d (speeder %d), "
                "held %d\n",
                stop, died, static_cast<unsigned>(kind), clone, static_cast<int>(clone_t), killer, speeder, held);
    if (stop >= 0) {
        CHECK(died < 0 && held == 0);
        return;
    }
    CHECK(died >= 280 && died <= 284 && held == 0 && kind == 8 && clone == static_cast<int32_t>(clone_t) && killer == speeder);
}

}  // namespace

int main() {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    std::printf("r3g t01: landing Land / Takeoff clip timing\n");
    test_landing_clips();
    std::printf("#128 t01: the landing service on counter-based streams\n");
    test_landing_streams();
    std::printf("#128 t04: the queues service on counter-based streams\n");
    test_queue_streams();
    std::printf("r3g t05: crush kind and Damage_Crush clones\n");
    test_crush_kind();
    std::printf("r3g t06: crouch under SPREAD_OUT\n");
    test_crouch();
    std::printf("mcx4 tac2: Elevated_Vulnerability on the landing unload\n");
    test_elevated_vulnerability();
    std::printf("mcx4 tac2: Elevated_Vulnerability on the space reinforcement drop\n");
    test_space_reinforcement_vulnerability();
    std::printf("mcx4 tac2: skirmish landing clearance delay\n");
    test_skirmish_landing_delay();
    std::printf("mcx4 tac2: Override_Death_Persistence_Duration\n");
    test_death_persistence_override();
    std::printf("mcx4 tac2: income streams\n");
    test_income_streams();
    std::printf("#44 t01: DEPLOY_TROOPERS in a land battle\n");
    test_deploy_troopers_battle();
    std::printf("#44 t01: a cable trip in a land battle\n");
    test_cable_trip_battle(-1);
    std::printf("#44 t03: a speeder stopped during the pull lets the walker go\n");
    test_cable_trip_battle(100);
    if (g_fail) {
        std::printf("alamo_sim_r3g_tests: %d failure(s)\n", g_fail);
        return 1;
    }
    std::printf("alamo_sim_r3g_tests: OK\n");
    return 0;
}
