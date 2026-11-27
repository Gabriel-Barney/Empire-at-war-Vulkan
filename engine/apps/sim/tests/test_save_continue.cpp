// alamo_sim_save_continue_tests (serves map3): save and continue on every battle fixture.
// Each fixture (battle_test_fixture.hpp sessions: a space battle, a land invasion, a space battle with a retreat, a land
// battle whose map carries placement records - hints, a company with a hint, a marker name, a build pad with a base
// level on a planet with a ground structure - roadmap map3, and #146 a space battle whose ships turn POWER_TO_WEAPONS
// on, so checkpoints fall in its active time and then in its recharge; #418 a land battle given a BOMBARD_DELAY story
// reward, TacticalBattle::reward_bombard_delay, at a battle frame; #419 a space battle with a fighter carrier whose
// launched squadron is killed at a battle frame, so the bay launches it again; #419 a land battle whose defenders are
// ordered into a bunker, GARRISON_STRUCTURE, at a battle frame; #420 a land battle with a weather scenario running and a
// space skirmish battle whose Empire buys a tech upgrade at a battle frame; #420 a space battle under a story session
// whose plot has a story-clock event and the battle's win / lose counters) runs twice from the same start:
//   continuous   ticked straight through;
//   reloaded     at every 64-tick checkpoint (the CRC golden checkpoints, ALAMO_CRC 2) the session is saved, a fresh
//                session is built like a new game, the save is loaded into it (a battle is rebuilt, then its parts are
//                read) and the run goes on in the fresh session.
// The checkpoint CRC records of both runs agree (every registered system, at every checkpoint, including the record
// the fresh session takes of the loaded state), and so do the answers the battle's Lua finders give after a load:
// Find_Hint / Get_Hint (TacticalBattle::map_hint, which lua::ObjectBridge::map_hint reads), the marker names and the
// pads' base levels, for every object of the battle; no object created after the battle start (after a load included)
// answers a map hint. #146: every ship's POWER_TO_WEAPONS state (active, ready) after a load equals the continuous run's
// at that checkpoint, and the ability fixture reaches both an active and a recharging checkpoint. #418: the battle's
// BOMBARD_DELAY override after a load equals the continuous run's at that checkpoint, and is set at some. #419: every
// carrier's hangar state (state, entries, live / remaining, next spawn) after a load equals the continuous run's, and the
// hangar fixture checkpoints both a launched squadron and the re-launch wait; every holder's occupants and walking-in
// count after a load equal the continuous run's, and the garrison fixture checkpoints a garrisoned bunker. #420: the
// weather state (scenario, type, intensity, phase) and the skirmish production state (every player's battle tech level and
// queue) after a load equal the continuous run's; the weather fixture checkpoints rain, the skirmish fixture the bought
// tech level; the story plot's event states (fired, armed, fire count, count) after a load equal the continuous run's,
// and the story fixture checkpoints its clock event fired and, after the battle, a battle counter. On every running
// battle the save ledger self-check holds too
// (TacticalBattle::check_save_ledger: state_io walks exactly the SAVED members of tactical_battle_ledger.hpp).
// Modes:
//   (none)                        every fixture at 0 workers
//   --workers N [--crc-out F]     every fixture at N workers; --crc-out writes the continuous runs' checkpoint records
//   [--golden F]                  and --golden compares them with that file (alamo_add_worker_count_check: 0 / 1 / 4)
#include "battle_test_fixture.hpp"
#include "alamo/lua/story_model.hpp"  // #420: the story fixture's plot readout

#include <bit>
#include <sstream>

namespace {

constexpr uint64_t kCheckpoint = 64;  // the golden checkpoint interval (docs/SIMULATION.md "CRC log formats")

struct Fixture {
    const char* name;
    bool land;
    float retreat_ratio;
    bool records;  // land map with placement records (hints, a hinted company, a marker name, a levelled pad)
    uint64_t ticks;
    uint64_t ability_frame = 0;  // #146: the battle frame at which every ship with POWER_TO_WEAPONS turns it on (0: never)
    uint64_t reward_frame = 0;   // #418: the battle frame of the BOMBARD_DELAY reward (0: never)
    float bombard_delay = 0.0f;  // its seconds
    uint64_t hangar_kill_frame = 0;  // #419: fighter data with a carrier; the battle frame its launched units are killed (0: no carrier)
    uint64_t garrison_frame = 0;     // #419: S4 data and a bunker; the battle frame the defenders are ordered into it (0: none)
    bool weather = false;            // #420: the land map's environment runs the Rain_Test weather scenario
    uint64_t skirmish_buy_frame = 0; // #420: a skirmish battle (production data); the frame the Empire buys S10_Tech_Upgrade
    bool story = false;              // #420: a story session (Story_Plots_SC.xml) runs from the start (story CRC on)
    uint64_t flagship_kill_frame = 0;  // #758: flagship data, an Underworld hero inside its lead; the frame the lead dies (0: none)
};

const Fixture kFixtures[] = {
    {"space", false, 0.0f, false, 2500},
    {"land", true, 0.0f, false, 3000},
    {"space_retreat", false, 0.95f, false, 2600},
    {"land_records", true, 0.0f, true, 3000},
    {"space_ability", false, 0.0f, false, 2500, 30},
    {"land_bombard_delay", true, 0.0f, false, 3000, 0, 40, 7.5f},
    {"space_hangar", false, 0.0f, false, 2500, 0, 0, 0.0f, 200},
    {"land_garrison", true, 0.0f, false, 3000, 0, 0, 0.0f, 0, 30},
    {"land_weather", true, 0.0f, false, 3000, 0, 0, 0.0f, 0, 0, true},
    {"space_skirmish", false, 0.0f, false, 2500, 0, 0, 0.0f, 0, 0, false, 2},
    {"space_story", false, 0.0f, false, 2500, 0, 0, 0.0f, 0, 0, false, 0, true},
    {"space_flagship", false, 0.0f, false, 2500, 0, 0, 0.0f, 0, 0, false, 0, false, 160},
};

// #420: the story fixture's file provider (made after its plots were written beside the default data)
std::unique_ptr<DirectoryProvider> g_story_files;

// #420: the weather fixture's tables (the Rain_Test scenario of test_battle_l1: CLEAR 4 s, RAIN 6 s), loaded once.
std::shared_ptr<const tactical::combat::WeatherData> g_weather;

constexpr auto kPowerToWeapons = static_cast<uint8_t>(tactical::abilities::kPowerToWeapons);

terrain::ObjectPlacement placement(const char* type, float x, float y, uint32_t owner) {
    terrain::ObjectPlacement p;
    p.type_crc = name_crc(type);
    p.owner = owner;
    p.position[0] = x;
    p.position[1] = y;
    return p;
}

// The land map with placement records: a company with a hint (its members inherit it), a
// trooper with an extra HINT, a hint and a marker name, and a hinted trooper of the attacker.
terrain::MapData records_map() {
    terrain::MapData m = land_map();
    terrain::ObjectPlacement::ExtraBehavior hint;
    hint.name_crc = name_crc("HINT");
    terrain::ObjectPlacement squad = placement("S10_Squad", 400.0f, 800.0f, 0);
    squad.hint = "squadhint";
    squad.extra_behaviors.push_back(hint);
    m.objects.push_back(squad);
    terrain::ObjectPlacement one = placement("S10_Trooper", 450.0f, 850.0f, 0);
    one.hint = "FalconGear1";
    one.extra_behaviors.push_back(hint);
    one.has_marker_name = true;
    one.marker_name = "inf4";
    m.objects.push_back(one);
    terrain::ObjectPlacement two = placement("S10_Trooper", 300.0f, 300.0f, 1);
    two.hint = "attackerhint";
    two.extra_behaviors.push_back(hint);
    two.has_marker_name = true;
    two.marker_name = "inf9";
    m.objects.push_back(two);
    // map3: a build pad with base level 1 (mini 0x0B; the planet's ground structure makes the battle's base level 1)
    terrain::ObjectPlacement pad = placement("S10_Build_Pad", 600.0f, 700.0f, 0);
    pad.placement_raw.push_back(terrain::RawMini{0x0B, {1}});
    m.objects.push_back(pad);
    return m;
}

// #419: the land map with a bunker (S4_Bunker, GARRISON_STRUCTURE) of the defender (player 0).
terrain::MapData garrison_map() {
    terrain::MapData m = land_map();
    m.objects.push_back(placement("S4_Bunker", 900.0f, 700.0f, 0));
    return m;
}

// #420: the weather fixture's land map: its one environment runs Rain_Test (as test_battle_l1's land map)
terrain::MapData weather_map() {
    terrain::MapData m = land_map();
    terrain::Environment env;
    env.weather_effect = "Rain_Test";
    m.environments.environments.push_back(env);
    return m;
}

// map3: the records fixture's extra types - a ground structure (its galactic copy at S10_B gives the land battle base
// level 1) and a build pad.
std::string records_types(std::string t) {
    const std::string end = "</S10>";
    const size_t at = t.rfind(end);
    if (at == std::string::npos) return t;
    t.insert(at, " <GroundStructure Name=\"S10_Barracks\"><Tactical_Health>500</Tactical_Health><Behavior>DUMMY_GROUND_STRUCTURE</Behavior>"
                 "</GroundStructure>\n <SpecialStructure Name=\"S10_Build_Pad\"><Behavior>TACTICAL_BUILD_OBJECTS</Behavior>"
                 "<Tactical_Health>500</Tactical_Health></SpecialStructure>\n");
    return t;
}

// #419: the hangar fixture's carrier waits 5 s before it launches again (150 frames: checkpoints fall in the wait) and
// holds two squadrons in reserve (so a killed squadron is launched again).
std::string hangar_types(std::string t) {
    const std::string from = "<Spawned_Squadron_Delay_Seconds>1.0</Spawned_Squadron_Delay_Seconds>";
    if (const size_t at = t.find(from); at != std::string::npos)
        t.replace(at, from.size(), "<Spawned_Squadron_Delay_Seconds>5.0</Spawned_Squadron_Delay_Seconds>"
                                   "<Reserve_Spawned_Units_Tech_0>S10_TIE_Squadron, 2</Reserve_Spawned_Units_Tech_0>");
    return t;
}

// One session of a fixture, built as run_campaign builds it. `launch`: the new-game start (the Underworld fleet moves
// to B); a session that is going to load a save skips it (the save holds the galaxy).
struct Session {
    std::unique_ptr<jobs::JobSystem> js;
    std::unique_ptr<SimHost> host;
    galactic::GalacticSim* gs = nullptr;
    std::unique_ptr<CampaignBattles> battles;
#ifdef ALAMO_SIM_HAVE_AI_TACTICAL
    std::vector<ai::tactical::TacticalBudgetCategory> budget = std::vector<ai::tactical::TacticalBudgetCategory>(1);
#endif
    bool land = false, invaded = false;
    uint64_t ability_frame = 0, reward_frame = 0, hangar_kill_frame = 0, garrison_frame = 0, skirmish_buy_frame = 0;
    float bombard_delay = 0.0f;
    bool ability_sent = false;  // #146: the fixture's POWER_TO_WEAPONS order was given (a loaded session carries it over)
    bool reward_sent = false;   // #418: the BOMBARD_DELAY reward was given (likewise)
    bool hangar_killed = false; // #419: the carrier's launched units were killed (likewise)
    bool garrison_sent = false; // #419: the garrison orders were given (likewise)
    bool skirmish_bought = false; // #420: the skirmish purchase was ordered (likewise)
    uint64_t flagship_kill_frame = 0;
    bool flagship_killed = false;  // #758: the lead was killed (likewise)
    objects::TypeIndex tie_type = objects::kNone;  // #419: S10_TIE (the hangar fixture's data)
    objects::TypeIndex base_type = objects::kNone, tech_type = objects::kNone;  // #420: S10_Star_Base_1, S10_Tech_Upgrade
    size_t crc_seen = 0;  // records of crc_log() already collected

    Session(const Data& d, const Fixture& f, uint32_t workers, bool launch)
        : land(f.land), ability_frame(f.ability_frame), reward_frame(f.reward_frame), hangar_kill_frame(f.hangar_kill_frame),
          garrison_frame(f.garrison_frame), skirmish_buy_frame(f.skirmish_buy_frame), bombard_delay(f.bombard_delay),
          flagship_kill_frame(f.flagship_kill_frame) {
        js = std::make_unique<jobs::JobSystem>(jobs::JobSystemConfig{static_cast<int32_t>(workers)});
        SimConfig cfg;
        cfg.seed = 2026;
        cfg.players = 4;
        cfg.story_crc = f.story;  // #420: the story session's state in the checkpoint records
        host = std::make_unique<SimHost>(d.types, *js, cfg);
        galactic::GalacticSetup setup = make_setup(d, f.land);
        if (f.records) setup.forces.push_back({0, 1, type_of(d, "S10_Barracks")});  // map3: battle base level 1
        if (f.skirmish_buy_frame > 0) {  // #420
            base_type = static_cast<objects::TypeIndex>(type_of(d, "S10_Star_Base_1"));
            tech_type = static_cast<objects::TypeIndex>(type_of(d, "S10_Tech_Upgrade"));
        }
        if (f.hangar_kill_frame > 0) {  // #419: the fighter carrier
            setup.forces.push_back({0, 1, type_of(d, "S10_Carrier")});
            tie_type = static_cast<objects::TypeIndex>(type_of(d, "S10_TIE"));
        }
        gs = host->start_galactic(d.rules, setup);
        CHECK(gs != nullptr);
        if (!gs) return;
        gs->set_emit_events(true);
        if (f.story) {  // #420: both sessions start the story (a load then replaces its state)
            StoryStart st;
            st.files = g_story_files.get();
            st.plot_sets = {{0, "Story_Plots_SC.xml"}};
            CHECK(host->start_story(st));
        }
        CampaignBattleOptions opt;
        opt.conflict.human_choice = galactic::BattleChoice::Fight;
        opt.battle.max_frames = 6000;
        opt.battle.retreat_ratio = f.retreat_ratio;
        opt.battle.fog = true;
        if (f.weather) opt.weather = g_weather;           // #420
        opt.battle.skirmish = f.skirmish_buy_frame > 0;   // #420
        opt.conflict.attach_to_flagship = f.flagship_kill_frame > 0;  // #758
#ifdef ALAMO_SIM_HAVE_AI_TACTICAL
        battles_budget_hook(opt, budget, *gs);
#endif
        const bool records = f.records, bunker = f.garrison_frame > 0, weather = f.weather;
        battles = std::make_unique<CampaignBattles>(*host, d.crules, *d.catalog,
                                                    [records, bunker, weather](const std::string& name, terrain::MapData& m) {
                                                        if (name == "S10_Space_B") m = space_map();
                                                        else if (name == "S10_Land_B")
                                                            m = records ? records_map() : bunker ? garrison_map() : weather ? weather_map() : land_map();
                                                        else return false;
                                                        return true;
                                                    },
                                                    opt);
        CHECK(battles->attach(nullptr));
        if (!launch) return;  // the save holds the galaxy (the caller carries `invaded` over)
        if (f.skirmish_buy_frame > 0) gs->add_credits(0, 1000.0f, false);  // #420: the purchase's credits
        if (f.land) {
            std::vector<objects::ObjectHandle> up;
            for (objects::ObjectHandle h : gs->planets()[2].surface)
                if (gs->store().owner(h) == 1) up.push_back(h);
            CHECK(gs->launch_units(1, 2, up) == commands::RejectReason::None);
        }
        if (f.flagship_kill_frame > 0) CHECK(gs->store().alive(board_flagship_hero(d, *gs, 1, 2)));  // #758
        const std::vector<uint32_t> uf = gs->fleets_at(2, 1);
        CHECK(!uf.empty());
        if (!uf.empty()) CHECK(gs->move_fleet(1, uf[0], 1) == commands::RejectReason::None);
    }

    void tick() {
        host->tick_once();
        if (land && !invaded && host->modes().depth() == 1) {
            const std::vector<uint32_t> f = gs->fleets_at(1, 1);
            if (!f.empty() && !gs->fleets()[f[0]].moving) {
                CHECK(gs->invade(1, 1, {}) == commands::RejectReason::None);
                invaded = true;
            }
        }
        if (ability_frame > 0 && !ability_sent)
            if (TacticalBattle* b = battle(); b && b->frame() >= ability_frame) {
                // #146: SetUnitAbilityMode on for each player's ships that have the ability (one order per owner, ascending id)
                const objects::ObjectStore& st = host->tactical()->primary();
                std::map<int32_t, std::vector<uint32_t>> by_owner;
                for (const objects::ObjectHandle h : st.all_objects())
                    if (st.alive(h) && st.owner(h) >= 0 && TacticalBattleTestAccess::abilities(*b).has_ability(st.id(h), kPowerToWeapons))
                        by_owner[st.owner(h)].push_back(static_cast<uint32_t>(st.id(h)));
                for (auto& [owner, ids] : by_owner) {
                    std::sort(ids.begin(), ids.end());
                    commands::EventData ev(commands::EventType::SetUnitAbilityMode);
                    ev.set("ability", kPowerToWeapons).set("on", 1).set_ids("units", ids);
                    CHECK(host->execute_direct(static_cast<uint8_t>(owner), commands::CommandSource::Player, ev) == commands::RejectReason::None);
                }
                CHECK(!by_owner.empty());
                ability_sent = true;
            }
        if (reward_frame > 0 && !reward_sent)
            if (TacticalBattle* b = battle(); b && b->frame() >= reward_frame) {
                b->reward_bombard_delay(bombard_delay);  // #418: as the STY1 t06 BOMBARD_DELAY reward sets it
                reward_sent = true;
            }
        if (hangar_kill_frame > 0 && !hangar_killed)
            if (TacticalBattle* b = battle(); b && b->frame() >= hangar_kill_frame && b->hangars()) {
                // #419: the launched squadron's fighters die (ascending id; the counted unit is the squadron's TEAM container,
                // which goes with its members), so the bay waits to launch again. Every S10_TIE here is a carrier launch.
                const objects::ObjectStore& st = host->tactical()->primary();
                std::vector<objects::ObjectId> fighters;
                for (const objects::ObjectHandle h : st.all_objects())
                    if (st.alive(h) && st.type(h) == tie_type) fighters.push_back(st.id(h));
                std::sort(fighters.begin(), fighters.end());
                CHECK(!fighters.empty());
                for (const objects::ObjectId id : fighters) CHECK(b->script_damage(*host->tactical(), id, 1.0e9f, false));
                hangar_killed = true;
            }
        if (garrison_frame > 0 && !garrison_sent)
            if (TacticalBattle* b = battle(); b && b->frame() >= garrison_frame) {
                // #419: every GARRISON_UNIT of the defender (player 0, ascending id) is ordered into its bunker
                const objects::ObjectStore& st = host->tactical()->primary();
                objects::ObjectId holder = objects::kNoObjectId;
                std::vector<objects::ObjectId> units;
                for (const objects::ObjectHandle h : st.all_objects()) {
                    if (!st.alive(h) || st.owner(h) != 0) continue;
                    if (b->garrison().is_holder(st.id(h))) holder = st.id(h);
                    else if (b->garrison().is_unit(st.id(h))) units.push_back(st.id(h));
                }
                std::sort(units.begin(), units.end());
                CHECK(holder != objects::kNoObjectId && !units.empty());
                for (const objects::ObjectId u : units) {
                    commands::EventData ev(commands::EventType::Garrison);
                    ev.set("unit", static_cast<uint32_t>(u)).set("garrison", static_cast<uint32_t>(holder));
                    (void)host->execute_direct(0, commands::CommandSource::Player, ev);  // a full bunker refuses the rest
                }
                garrison_sent = true;
            }
        if (flagship_kill_frame > 0 && !flagship_killed)
            if (TacticalBattle* b = battle(); b && b->frame() >= flagship_kill_frame) {
                // #758: the Underworld lead dies (Die), so the battle is saved with its hero inside and released
                CHECK(b->contained().size() == 1);
                const objects::ObjectId lead = b->contained().empty() ? objects::kNoObjectId : b->contained_host(0);
                CHECK(lead != objects::kNoObjectId);
                if (lead != objects::kNoObjectId) TacticalBattleTestAccess::kill(*b, *host->tactical(), lead);
                flagship_killed = true;
            }
        if (skirmish_buy_frame > 0 && !skirmish_bought)
            if (TacticalBattle* b = battle(); b && b->frame() >= skirmish_buy_frame) {
                // #420: the Empire buys S10_Tech_Upgrade (Tactical_Build_Increments_Tech_Level, 2 s) at its star base
                const objects::ObjectStore& st = host->tactical()->primary();
                objects::ObjectId station = objects::kNoObjectId;
                for (const objects::ObjectHandle h : st.owned_by(0))
                    if (st.alive(h) && st.type(h) == base_type) station = st.id(h);
                CHECK(station != objects::kNoObjectId);
                commands::EventData ev(commands::EventType::Production);
                ev.set("type_index", static_cast<uint32_t>(tech_type)).set("location", static_cast<uint32_t>(station));
                CHECK(host->execute_direct(0, commands::CommandSource::Player, ev) == commands::RejectReason::None);
                skirmish_bought = true;
            }
    }

    TacticalBattle* battle() const {
        TacticalBattle* b = battles ? battles->battle() : nullptr;
        return b && host->tactical() && b->frame() >= 1 ? b : nullptr;
    }
};

// A checkpoint record with its system names (a fresh session may register them in another order).
struct Checkpoint {
    uint64_t tick = 0, combined = 0;
    uint32_t parity = 0;
    std::map<std::string, uint64_t> systems;
    friend bool operator==(const Checkpoint&, const Checkpoint&) = default;
};

void collect(Session& s, std::vector<Checkpoint>& out) {
    const CrcLog& log = s.host->crc_log();
    for (; s.crc_seen < log.records.size(); ++s.crc_seen) {
        const CrcRecord& r = log.records[s.crc_seen];
        if (r.tick % kCheckpoint != 0) continue;
        Checkpoint c;
        c.tick = r.tick;
        c.combined = r.combined;
        c.parity = r.parity;
        for (size_t i = 0; i < r.systems.size() && i < log.systems.size(); ++i) c.systems[log.systems[i]] = r.systems[i];
        out.push_back(std::move(c));
    }
}

// What the battle's Lua finders answer per object: Find_Hint / Get_Hint / Find_All_Objects_With_Hint read
// TacticalBattle::map_hint through lua::ObjectBridge::map_hint; the marker name is the placement's 0x46E record; the
// pad level is the map pad's base level (AI_GALACTIC_TOKENS 9.4, the persistence record's level).
std::vector<std::string> lookup_answers(const TacticalBattle& b, const TacticalMode& tm) {
    std::vector<std::string> out;
    const objects::ObjectStore& st = tm.primary();
    for (const objects::ObjectHandle h : st.all_objects()) {
        if (!st.exists(h)) continue;
        const objects::ObjectId id = st.id(h);
        const std::string_view hint = b.map_hint(id), marker = b.map_marker_name(id);
        const int32_t level = b.pad_base_level(id);
        if (hint.empty() && marker.empty() && level == 0) continue;
        out.push_back(std::to_string(id) + " hint '" + std::string(hint) + "' marker '" + std::string(marker) + "' pad " +
                      std::to_string(level));
    }
    std::sort(out.begin(), out.end());
    return out;
}

// #146: every object's POWER_TO_WEAPONS state at a checkpoint ("id active ready"), and whether any is active / recharging
// (neither active nor ready: this space map has no nebula, ion stun or force lockout).
struct AbilityReadout {
    std::vector<std::string> lines;
    bool any_active = false, any_recharging = false;
};
AbilityReadout ability_readout(TacticalBattle& b, const TacticalMode& tm) {
    AbilityReadout out;
    const tactical::abilities::AbilitySystem& ab = TacticalBattleTestAccess::abilities(b);
    const objects::ObjectStore& st = tm.primary();
    for (const objects::ObjectHandle h : st.all_objects()) {
        if (!st.alive(h)) continue;
        const objects::ObjectId id = st.id(h);
        if (!ab.has_ability(id, kPowerToWeapons)) continue;
        const bool active = ab.is_active(id, kPowerToWeapons), ready = ab.is_ready(id, kPowerToWeapons, b.frame());
        out.any_active = out.any_active || active;
        out.any_recharging = out.any_recharging || (!active && !ready);
        out.lines.push_back(std::to_string(id) + (active ? " active" : " -") + (ready ? " ready" : " -"));
    }
    std::sort(out.lines.begin(), out.lines.end());
    return out;
}

// #419: every carrier's hangar state ("id state s entries n [type live remaining]... next f"); `live` / `wait`: some entry
// has a launched unit alive / has fewer live than its start count with launches left (the bay waits to launch again).
std::vector<std::string> hangar_readout(const TacticalBattle& b, const TacticalMode& tm, bool& live, bool& wait) {
    std::vector<std::string> out;
    const tactical::hangar::HangarSystem* hs = b.hangars();
    if (!hs) return out;
    const objects::ObjectStore& st = tm.primary();
    for (const objects::ObjectHandle h : st.all_objects()) {
        if (!st.exists(h) || !hs->has_carrier(st.id(h))) continue;
        const objects::ObjectId id = st.id(h);
        std::string line = std::to_string(id) + " state " + std::to_string(hs->state(id)) + " entries " + std::to_string(hs->entry_count(id));
        for (uint32_t e = 0; e < hs->entry_count(id); ++e) {
            const int32_t n = hs->live(id, e), rem = hs->remaining(id, e);
            line += " [" + std::to_string(hs->entry_type(id, e)) + " " + std::to_string(n) + " " + std::to_string(rem) + "]";
            live = live || n > 0;
            wait = wait || (n < hs->max_alive(id, e) && rem != 0);
        }
        line += " next " + std::to_string(hs->next_spawn_frame(id));
        out.push_back(std::move(line));
    }
    std::sort(out.begin(), out.end());
    return out;
}

// #419: every garrison holder's state ("id walking-in n: occupant ids in entry order"); `occupied`: some holder has one.
std::vector<std::string> garrison_readout(const TacticalBattle& b, const TacticalMode& tm, bool& occupied) {
    std::vector<std::string> out;
    const tactical::garrison::GarrisonSystem& g = b.garrison();
    const objects::ObjectStore& st = tm.primary();
    for (const objects::ObjectHandle h : st.all_objects()) {
        if (!st.exists(h) || !g.is_holder(st.id(h))) continue;
        const objects::ObjectId id = st.id(h);
        std::string line = std::to_string(id) + " walking-in " + std::to_string(g.reserved(id)) + ":";
        for (const tactical::garrison::ObjectId o : g.occupants(id)) line += " " + std::to_string(o);
        occupied = occupied || !g.occupants(id).empty();
        out.push_back(std::move(line));
    }
    std::sort(out.begin(), out.end());
    return out;
}

struct RunResult {
    std::vector<Checkpoint> checkpoints;
    std::map<uint64_t, std::vector<std::string>> answers;  // checkpoint tick -> lookup answers (the reloaded run: after the load)
    std::map<uint64_t, std::vector<std::string>> abilities;  // #146: checkpoint tick -> POWER_TO_WEAPONS states (likewise)
    std::map<uint64_t, float> bombard_delays;                 // #418: checkpoint tick -> the BOMBARD_DELAY override (likewise)
    std::map<uint64_t, std::vector<std::string>> hangar;      // #419: checkpoint tick -> every carrier's hangar state (likewise)
    std::map<uint64_t, std::vector<std::string>> garrisons;   // #419: checkpoint tick -> every holder's occupants (likewise)
    bool garrisoned_seen = false;                             // #419: some holder had an occupant
    std::map<uint64_t, std::string> weather;                  // #420: checkpoint tick -> the weather state (likewise)
    std::map<uint64_t, std::string> skirmish;                 // #420: checkpoint tick -> tech levels and queues (likewise)
    std::map<uint64_t, std::vector<std::string>> story;       // #420: checkpoint tick -> the plot's event states (every checkpoint)
    bool story_clock_seen = false, story_battle_seen = false; // #420: the clock event fired / a battle counter counted
    bool rain_seen = false, tech_seen = false;                // #420: rain at a checkpoint / the bought tech level
    bool ability_active_seen = false, ability_recharge_seen = false, bombard_delay_seen = false;
    bool hangar_live_seen = false, hangar_wait_seen = false;  // #419: a launched unit alive / a launch pending
    uint32_t loads_inside = 0, loads_released = 0;  // #758: battle loads with the flagship record inside / released
    uint32_t battle_checkpoints = 0, reloads = 0, battle_reloads = 0, refused = 0;
    uint32_t ledger_checks = 0;
    bool ledger_ok = true, load_ok = true;
    uint64_t last_tick = 0;

    // #420: the story plot's event states ("plot event fired armed fires count"), at every checkpoint (galactic too)
    void record_story(uint64_t tick, const SimHost& host) {
        const StorySession* s = host.story();
        if (!s || s->plot_set_count() == 0) return;
        std::vector<std::string> lines;
        const lua::StoryModel& m = s->model(0);
        for (uint32_t i = 0; i < m.plot_count(); ++i) {
            const lua::StoryPlot& p = m.plot(i);
            for (size_t e = 0; e < p.events.size() && e < p.state.size(); ++e) {
                const lua::StoryEventState& st = p.state[e];
                lines.push_back(p.name + " " + p.events[e].name + " " + (st.fired ? "fired" : "-") + " " + (st.armed ? "armed" : "-") + " " +
                                std::to_string(st.fire_count) + " " + std::to_string(st.count));
                if (st.fired && p.events[e].name == "Clock") story_clock_seen = true;
                if (st.count > 0 || (st.fired && p.events[e].name != "Clock")) story_battle_seen = true;
            }
        }
        story[tick] = std::move(lines);
    }

    // the lookup answers and ability readout of a battle at checkpoint `tick`
    void record(uint64_t tick, TacticalBattle& b, const TacticalMode& tm) {
        answers[tick] = lookup_answers(b, tm);
        AbilityReadout a = ability_readout(b, tm);
        ability_active_seen = ability_active_seen || a.any_active;
        ability_recharge_seen = ability_recharge_seen || a.any_recharging;
        abilities[tick] = std::move(a.lines);
        const float bd = TacticalBattleTestAccess::bombard_delay_override(b);
        bombard_delays[tick] = bd;
        bombard_delay_seen = bombard_delay_seen || bd > 0.0f;
        hangar[tick] = hangar_readout(b, tm, hangar_live_seen, hangar_wait_seen);
        garrisons[tick] = garrison_readout(b, tm, garrisoned_seen);
        {  // #420: the weather state, the floats bit-exact
            const tactical::combat::WeatherSystem& w = b.weather();
            const float in = w.intensity();
            const auto bits = std::bit_cast<uint32_t>(in);
            weather[tick] = std::to_string(w.active() ? 1 : 0) + " type " + std::to_string(static_cast<int>(w.type())) + " intensity " +
                            std::to_string(bits) + " phase " + std::to_string(w.phase_index()) + (w.transitioning() ? " easing" : "");
            rain_seen = rain_seen || (w.type() == tactical::combat::WeatherType::Rain && in > 0.0f);
        }
        {  // #420: every player's battle tech level and production queue
            std::string line;
            for (int32_t p = 0; p < 3; ++p) {  // the test galaxy's three players (make_setup: Empire, Underworld, Neutral)
                line += "p" + std::to_string(p) + " tech " + std::to_string(b.battle_tech_level(p));
                if (b.production())
                    for (const tactical::hangar::BuildOrder& o : b.production()->queue(p))
                        line += " [" + std::to_string(o.id) + " " + std::to_string(o.type) + " " + std::to_string(o.builder) + "]";
                line += "; ";
            }
            skirmish[tick] = line;
            tech_seen = tech_seen || TacticalBattleTestAccess::tech_bonus(b, 0) > 0;  // the upgrade's +1 (no other source here)
        }
    }
};

void check_ledger(const TacticalBattle& b, RunResult& r, const char* fixture) {
    std::string err;
    ++r.ledger_checks;
    if (!b.check_save_ledger(&err)) {
        if (r.ledger_ok) std::printf("  %s: save ledger: %s\n", fixture, err.c_str());
        r.ledger_ok = false;
    }
}

RunResult run(const Data& d, const Fixture& f, uint32_t workers, bool reload) {
    RunResult r;
    auto s = std::make_unique<Session>(d, f, workers, true);
    if (!s->gs) return r;
    for (uint64_t t = 0; t < f.ticks; ++t) {
        s->tick();
        collect(*s, r.checkpoints);
        const uint64_t tick = s->host->frame();
        r.last_tick = tick;
        if (tick % kCheckpoint != 0) continue;
        if (!reload) r.record_story(tick, *s->host);  // #420
        TacticalBattle* b = s->battle();
        if (b) {
            ++r.battle_checkpoints;
            check_ledger(*b, r, f.name);
            if (!reload) r.record(tick, *b, *s->host->tactical());
        }
        if (!reload) continue;
        std::string why;
        if (!s->host->save_allowed(&why)) {
            ++r.refused;
            continue;
        }
        SaveFile save;
        if (!s->host->save(save, &why)) {
            std::printf("  %s: save at tick %" PRIu64 " failed: %s\n", f.name, tick, why.c_str());
            r.load_ok = false;
            break;
        }
        auto fresh = std::make_unique<Session>(d, f, workers, false);
        if (!fresh->gs) break;
        fresh->invaded = s->invaded;  // the test's own invasion order, given once (the save holds its effect)
        fresh->ability_sent = s->ability_sent;  // #146: likewise the ability order
        fresh->reward_sent = s->reward_sent;    // #418: and the reward
        fresh->hangar_killed = s->hangar_killed;  // #419: and the kill
        fresh->garrison_sent = s->garrison_sent;  // #419: and the garrison orders
        fresh->skirmish_bought = s->skirmish_bought;  // #420: and the purchase
        fresh->flagship_killed = s->flagship_killed;  // #758: and the lead's death
        LoadReport rep;
        if (!fresh->host->load(save, &why, &rep)) {
            std::printf("  %s: load at tick %" PRIu64 " failed: %s\n", f.name, tick, rep.text().c_str());
            r.load_ok = false;
            break;
        }
        CHECK(fresh->host->frame() == tick);
        ++r.reloads;
        s = std::move(fresh);
        r.record_story(tick, *s->host);  // #420: the loaded story state
        if (TacticalBattle* lb = s->battle()) {
            ++r.battle_reloads;
            for (const TacticalBattle::ContainedUnit& c : lb->contained()) {  // #758
                r.loads_inside += c.state == TacticalBattle::ContainedState::Inside ? 1u : 0u;
                r.loads_released += c.state == TacticalBattle::ContainedState::Released ? 1u : 0u;
            }
            check_ledger(*lb, r, f.name);
            r.record(tick, *lb, *s->host->tactical());
        }
    }
    return r;
}

// The first difference of two checkpoint lists, by tick (the reloaded run has the loaded state's record twice).
std::string first_difference(const std::vector<Checkpoint>& want, const std::vector<Checkpoint>& got) {
    std::map<uint64_t, const Checkpoint*> by_tick;
    for (const Checkpoint& c : want) by_tick[c.tick] = &c;
    for (const Checkpoint& c : got) {
        const auto it = by_tick.find(c.tick);
        if (it == by_tick.end()) return "tick " + std::to_string(c.tick) + " is not a checkpoint of the continuous run";
        if (*it->second == c) continue;
        std::ostringstream o;
        o << "tick " << c.tick << ":";
        for (const auto& [name, v] : it->second->systems) {
            const auto g = c.systems.find(name);
            if (g == c.systems.end()) o << " " << name << " missing";
            else if (g->second != v) o << " " << name;
        }
        for (const auto& [name, v] : c.systems)
            if (!it->second->systems.count(name)) o << " " << name << " extra";
        if (it->second->parity != c.parity) o << " parity";
        return o.str();
    }
    std::map<uint64_t, bool> seen;
    for (const Checkpoint& c : got) seen[c.tick] = true;
    for (const Checkpoint& c : want)
        if (!seen.count(c.tick)) return "checkpoint " + std::to_string(c.tick) + " missing from the reloaded run";
    return {};
}

std::string checkpoints_text(const std::vector<Checkpoint>& cps) {
    std::ostringstream o;
    for (const Checkpoint& c : cps) {
        o << c.tick << " " << std::hex << c.combined << " " << c.parity;
        for (const auto& [name, v] : c.systems) o << " " << name << "=" << v;
        o << std::dec << "\n";
    }
    return o.str();
}

std::string test_fixture(const Data& d, const Fixture& f, uint32_t workers) {
    const RunResult a = run(d, f, workers, false);
    const RunResult b = run(d, f, workers, true);
    std::printf("  %s (%u workers): %zu checkpoints (%u in battle), %u reloads (%u in battle, %u refused), %u ledger checks\n", f.name,
                workers, a.checkpoints.size(), a.battle_checkpoints, b.reloads, b.battle_reloads, b.refused,
                a.ledger_checks + b.ledger_checks);
    CHECK(a.battle_checkpoints >= 4);   // the fixture's battle spans several checkpoints
    CHECK(b.battle_reloads >= 4);       // and was saved / rebuilt / loaded at them
    CHECK(a.ledger_ok && b.ledger_ok);  // state_io walks the ledger's SAVED members
    CHECK(b.load_ok);
    CHECK(a.last_tick == b.last_tick);
    if (f.flagship_kill_frame > 0) {  // #758: saved and loaded with the hero inside its lead and after its release
        std::printf("  %s: %u loads with the hero inside, %u after its release\n", f.name, b.loads_inside, b.loads_released);
        CHECK(b.loads_inside >= 1 && b.loads_released >= 1);
    }
    const std::string diff = first_difference(a.checkpoints, b.checkpoints);
    if (!diff.empty()) std::printf("  %s: checkpoint CRCs differ after a load: %s\n", f.name, diff.c_str());
    CHECK(diff.empty());
    uint32_t compared = 0, hinted = 0, padded = 0;
    // map3: the map load names every object with a hint, a marker name or a pad level; an object created later (also one
    // created after a load, which a rebuild-era id table would have named) answers none of them.
    std::map<std::string, bool> first_ids;
    if (!a.answers.empty())
        for (const std::string& line : a.answers.begin()->second) first_ids[line.substr(0, line.find(' '))] = true;
    for (const auto& [tick, got] : b.answers)
        for (const std::string& line : got)
            if (!first_ids.count(line.substr(0, line.find(' ')))) {
                std::printf("  %s: tick %" PRIu64 ": an object created after the map load answers '%s'\n", f.name, tick, line.c_str());
                CHECK(false);
            }
    for (const auto& [tick, want] : a.answers) {
        const auto it = b.answers.find(tick);
        if (it == b.answers.end()) continue;  // no battle at that checkpoint of the reloaded run
        ++compared;
        hinted += want.empty() ? 0u : 1u;
        for (const std::string& line : want) padded += line.size() > 6 && line.compare(line.size() - 6, 6, " pad 1") == 0 ? 1u : 0u;
        if (it->second != want) {
            std::printf("  %s: lookup answers differ after the load at tick %" PRIu64 " (%zu vs %zu):\n", f.name, tick,
                        want.size(), it->second.size());
            for (size_t i = 0; i < std::max(want.size(), it->second.size()) && i < 6; ++i)
                std::printf("    want %-48s got %s\n", i < want.size() ? want[i].c_str() : "-",
                            i < it->second.size() ? it->second[i].c_str() : "-");
            CHECK(false);
            break;
        }
    }
    CHECK(compared == b.battle_reloads);
    for (const auto& [tick, want] : a.abilities) {  // #146: the ability states a load gives back
        const auto it = b.abilities.find(tick);
        if (it == b.abilities.end() || it->second == want) continue;
        std::printf("  %s: POWER_TO_WEAPONS states differ after the load at tick %" PRIu64 " (%zu vs %zu):\n", f.name, tick, want.size(),
                    it->second.size());
        for (size_t i = 0; i < std::max(want.size(), it->second.size()) && i < 6; ++i)
            std::printf("    want %-24s got %s\n", i < want.size() ? want[i].c_str() : "-", i < it->second.size() ? it->second[i].c_str() : "-");
        CHECK(false);
        break;
    }
    if (f.ability_frame > 0) {  // the ability fixture is saved and loaded while the ability runs and while it recharges
        if (!(a.ability_active_seen && a.ability_recharge_seen && b.ability_active_seen && b.ability_recharge_seen))
            std::printf("  %s: active seen %d/%d, recharge seen %d/%d\n", f.name, a.ability_active_seen, b.ability_active_seen,
                        a.ability_recharge_seen, b.ability_recharge_seen);
        CHECK(a.ability_active_seen && a.ability_recharge_seen && b.ability_active_seen && b.ability_recharge_seen);
    }
    for (const auto& [tick, want] : a.bombard_delays) {  // #418: the BOMBARD_DELAY override a load gives back
        const auto it = b.bombard_delays.find(tick);
        if (it == b.bombard_delays.end() || it->second == want) continue;
        std::printf("  %s: BOMBARD_DELAY override differs after the load at tick %" PRIu64 ": want %.2f, got %.2f\n", f.name, tick,
                    static_cast<double>(want), static_cast<double>(it->second));
        CHECK(false);
        break;
    }
    for (const auto& [tick, want] : a.hangar) {  // #419: the hangar state a load gives back
        const auto it = b.hangar.find(tick);
        if (it == b.hangar.end() || it->second == want) continue;
        std::printf("  %s: hangar states differ after the load at tick %" PRIu64 ":\n", f.name, tick);
        for (size_t i = 0; i < std::max(want.size(), it->second.size()) && i < 4; ++i)
            std::printf("    want %-40s got %s\n", i < want.size() ? want[i].c_str() : "-", i < it->second.size() ? it->second[i].c_str() : "-");
        CHECK(false);
        break;
    }
    for (const auto& [tick, want] : a.garrisons) {  // #419: the garrison state a load gives back
        const auto it = b.garrisons.find(tick);
        if (it == b.garrisons.end() || it->second == want) continue;
        std::printf("  %s: garrisons differ after the load at tick %" PRIu64 ":\n", f.name, tick);
        for (size_t i = 0; i < std::max(want.size(), it->second.size()) && i < 4; ++i)
            std::printf("    want %-40s got %s\n", i < want.size() ? want[i].c_str() : "-", i < it->second.size() ? it->second[i].c_str() : "-");
        CHECK(false);
        break;
    }
    for (const auto& [tick, want] : a.weather) {  // #420: the weather state a load gives back
        const auto it = b.weather.find(tick);
        if (it == b.weather.end() || it->second == want) continue;
        std::printf("  %s: weather differs after the load at tick %" PRIu64 ": want %s, got %s\n", f.name, tick, want.c_str(),
                    it->second.c_str());
        CHECK(false);
        break;
    }
    for (const auto& [tick, want] : a.skirmish) {  // #420: the tech levels and queues a load gives back
        const auto it = b.skirmish.find(tick);
        if (it == b.skirmish.end() || it->second == want) continue;
        std::printf("  %s: skirmish production differs after the load at tick %" PRIu64 ":\n    want %s\n    got  %s\n", f.name, tick,
                    want.c_str(), it->second.c_str());
        CHECK(false);
        break;
    }
    for (const auto& [tick, want] : a.story) {  // #420: the plot state a load gives back
        const auto it = b.story.find(tick);
        if (it == b.story.end() || it->second == want) continue;
        std::printf("  %s: story event states differ after the load at tick %" PRIu64 ":\n", f.name, tick);
        for (size_t i = 0; i < std::max(want.size(), it->second.size()) && i < 4; ++i)
            std::printf("    want %-48s got %s\n", i < want.size() ? want[i].c_str() : "-", i < it->second.size() ? it->second[i].c_str() : "-");
        CHECK(false);
        break;
    }
    if (f.story) {
        std::printf("  %s: story clock event seen %d / %d, a battle counter %d / %d\n", f.name, a.story_clock_seen ? 1 : 0,
                    b.story_clock_seen ? 1 : 0, a.story_battle_seen ? 1 : 0, b.story_battle_seen ? 1 : 0);
        CHECK(a.story_clock_seen && b.story_clock_seen && a.story_battle_seen && b.story_battle_seen);
    }
    if (f.weather) {
        std::printf("  %s: rain seen %d / after a load %d\n", f.name, a.rain_seen ? 1 : 0, b.rain_seen ? 1 : 0);
        CHECK(a.rain_seen && b.rain_seen);
    }
    if (f.skirmish_buy_frame > 0) {
        std::printf("  %s: bought tech level seen %d / after a load %d\n", f.name, a.tech_seen ? 1 : 0, b.tech_seen ? 1 : 0);
        CHECK(a.tech_seen && b.tech_seen);
    }
    if (f.garrison_frame > 0) {  // the garrison fixture is saved and loaded with the bunker occupied
        std::printf("  %s: a garrisoned holder seen %d / after a load %d\n", f.name, a.garrisoned_seen ? 1 : 0, b.garrisoned_seen ? 1 : 0);
        CHECK(a.garrisoned_seen && b.garrisoned_seen);
    }
    if (f.hangar_kill_frame > 0) {  // the hangar fixture is saved and loaded with a squadron out and with the bay waiting
        std::printf("  %s: hangar launched seen %d / %d, re-launch wait seen %d / %d\n", f.name, a.hangar_live_seen ? 1 : 0,
                    b.hangar_live_seen ? 1 : 0, a.hangar_wait_seen ? 1 : 0, b.hangar_wait_seen ? 1 : 0);
        CHECK(a.hangar_live_seen && b.hangar_live_seen && a.hangar_wait_seen && b.hangar_wait_seen);
    }
    if (f.reward_frame > 0) {  // the reward fixture is saved and loaded while the override is set
        std::printf("  %s: BOMBARD_DELAY %.2f s seen at a checkpoint %d / after a load %d\n", f.name, static_cast<double>(f.bombard_delay),
                    a.bombard_delay_seen ? 1 : 0, b.bombard_delay_seen ? 1 : 0);
        CHECK(a.bombard_delay_seen && b.bombard_delay_seen);
    }
    if (f.records) CHECK(hinted == compared && compared > 0);  // the records fixture has hints at every checkpoint
    if (f.records) CHECK(padded == compared);                  // and its pad keeps its base level
    return "# " + std::string(f.name) + "\n" + checkpoints_text(a.checkpoints);
}

}  // namespace

int main(int argc, char** argv) {
    uint32_t workers = 0;
    std::string out_path, golden;
    for (int i = 1; i + 1 < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--workers") workers = static_cast<uint32_t>(std::strtoul(argv[i + 1], nullptr, 10));
        else if (a == "--crc-out") out_path = argv[i + 1];
        else if (a == "--golden") golden = argv[i + 1];
    }
    static std::string prefix;  // a data folder per worker count (the ctest entries run side by side)
    prefix = "alamo_sim_save_continue_w" + std::to_string(workers);
    g_temp_prefix = prefix.c_str();
    std::printf("REF2 t01: save and continue at every %" PRIu64 "-tick checkpoint, %u workers\n", kCheckpoint, workers);
    auto d = make_data();
    // map3: the records fixture's data adds a ground structure and a build pad (its own temp folder)
    static std::string records_prefix;
    records_prefix = prefix + "_records";
    g_temp_prefix = records_prefix.c_str();
    g_types_patch = records_types;
    auto dr = make_data();
    g_types_patch = nullptr;
    // #419: the hangar fixture's data (the fighter types: a TIE squadron and a carrier with one bay)
    static std::string hangar_prefix;
    hangar_prefix = prefix + "_hangar";
    g_temp_prefix = hangar_prefix.c_str();
    g_types_patch = hangar_types;
    auto dh = make_data(false, false, false, false, false, false, false, false, true);
    g_types_patch = nullptr;
    // #419: the garrison fixture's data (S4: troopers are GARRISON_UNIT, the S4_Bunker type)
    static std::string garrison_prefix;
    garrison_prefix = prefix + "_garrison";
    g_temp_prefix = garrison_prefix.c_str();
    auto dg = make_data(false, false, true);
    // #420: the skirmish fixture's data (station production: the star base lists S10_Tech_Upgrade)
    static std::string skirmish_prefix;
    skirmish_prefix = prefix + "_skirmish";
    g_temp_prefix = skirmish_prefix.c_str();
    auto dk = make_data(false, false, false, false, true);
    // #758: the flagship fixture's data (a hero team whose member has Attach_To_Flagship_During_Space_Battle)
    static std::string flagship_prefix;
    flagship_prefix = prefix + "_flagship";
    g_temp_prefix = flagship_prefix.c_str();
    g_types_patch = flagship_hero_types;
    auto df = make_data();
    g_types_patch = nullptr;
    g_temp_prefix = prefix.c_str();
    {  // #420: the weather tables beside the default data (only the weather fixture's battles get them)
        const fs::path x = d->root / "Data/XML";
        write_file(x / "WeatherScenarios.xml",
                   "<Weather_Scenarios><Weather_Scenario name=\"Rain_Test\"><Phase name=\"CLEAR\"><Emitter_Intensity>0.0</Emitter_Intensity>"
                   "<Duration>4</Duration><Ease_Out_Duration>2</Ease_Out_Duration></Phase><Phase name=\"RAIN\"><Emitter_Intensity>1.0"
                   "</Emitter_Intensity><Duration>6</Duration><Ease_Out_Duration>2</Ease_Out_Duration></Phase></Weather_Scenario>"
                   "</Weather_Scenarios>\n");
        write_file(x / "WeatherModifiers.xml",
                   "<WeatherModifies><WeatherModifier name=\"rain\"><Sight_Range_Modifier name=\"Infantry\">0.5f</Sight_Range_Modifier>"
                   "</WeatherModifier></WeatherModifies>\n");
        auto wd = std::make_shared<tactical::combat::WeatherData>();
        const DirectoryProvider files(path_to_utf8(d->root));  // a fresh index: the files were written after make_data
        CHECK(tactical::combat::load_weather_data(files, *wd));
        g_weather = std::move(wd);
        // #420: the story fixture's plot: a story-clock event (5 s) and the space battle's win / lose counters
        write_file(x / "Story_Plots_SC.xml", "<Story_Mode_Plots><Active_Plot>Story_SC.xml</Active_Plot></Story_Mode_Plots>\n");
        write_file(x / "Story_SC.xml",
                   "<Story>\n"
                   " <Event Name=\"Clock\"><Event_Type>STORY_ELAPSED</Event_Type><Event_Param1>5</Event_Param1></Event>\n"
                   " <Event Name=\"Won_Space\"><Event_Type>STORY_WIN_BATTLES</Event_Type><Event_Param1>1</Event_Param1>"
                   "<Event_Param2>SPACE</Event_Param2></Event>\n"
                   " <Event Name=\"Lost_Space\"><Event_Type>STORY_LOSE_BATTLES</Event_Type><Event_Param1>1</Event_Param1>"
                   "<Event_Param2>SPACE</Event_Param2></Event>\n"
                   "</Story>\n");
        g_story_files = std::make_unique<DirectoryProvider>(path_to_utf8(d->root));
    }
    std::string text;
    for (const Fixture& f : kFixtures)
        text += test_fixture(f.records                ? *dr
                             : f.hangar_kill_frame > 0  ? *dh
                             : f.garrison_frame > 0     ? *dg
                             : f.skirmish_buy_frame > 0 ? *dk
                             : f.flagship_kill_frame > 0 ? *df
                                                        : *d,
                             f, workers);
    if (!out_path.empty()) {
        std::ofstream o(out_path, std::ios::binary | std::ios::trunc);
        o << text;
        CHECK(static_cast<bool>(o));
    }
    if (!golden.empty()) {
        std::ifstream in(golden, std::ios::binary);
        std::stringstream want;
        want << in.rdbuf();
        CHECK(!want.str().empty());
        if (want.str() != text) {
            std::printf("checkpoint records differ from the 0-worker run (%s)\n", golden.c_str());
            ++g_fail;
        }
    }
    std::error_code ec;
    remove_all_long(d->root, ec);
    remove_all_long(dr->root, ec);
    remove_all_long(dh->root, ec);
    remove_all_long(dg->root, ec);
    remove_all_long(dk->root, ec);
    remove_all_long(df->root, ec);
    if (g_fail) {
        std::printf("%d check(s) FAILED\n", g_fail);
        return 1;
    }
    std::printf("all save-and-continue checks passed\n");
    return 0;
}
