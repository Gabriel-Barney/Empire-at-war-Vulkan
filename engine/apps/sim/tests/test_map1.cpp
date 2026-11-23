// alamo_sim_map1_tests (roadmap group map1): the battle's map load and start placement.
//   default (fast, synthetic data, battle_test_fixture.hpp):
//     * t03 a company placed on a land map is expanded into its members at the placement (Q6_T30 1, 0x2ab060), the
//       members carry the placement's hint and HINT;
//     * t04 a record's 0x4B7 extra behaviour (HINT) is attached and its 0x46E marker name kept;
//     * t05 a hangar placement with mini 0x29 = 0 has spawning off on a campaign / map battle too, one without the
//       mini keeps it on (BKG3 Q3.2);
//     * t06 a structure is placed on its remembered marker (pass 1) and the placement writes the galactic memory
//       (REMAINING_GAPS 3.2 step 4);
//     * t07 space start: the DUMMY_STAR_BASE of the battle's starbase level for the defender, the start units'
//       Space_Escort_Unit_Types, the defender's camera focus on the station marker (REMAINING_GAPS 3.2 steps 1 / 6).
//     * #44 t02 (R3G_ANIM_TIMED_RE §3; 0x594dc0 / 0x593960 read 2026-11-23): a land SPAWN_SQUADRON structure with
//       Spawned_Squadron_Location_Bones sends its unit along the spawn bone's x axis, not along the structure's facing;
//       a Spawn_Garrison_On_Load one creates its garrison at the fly-out point, facing along the bone, with no order.
//     * #57 t05: a map object that is a special weapon (LOBBING_SUPERWEAPON) keeps its SpecialWeaponSystem registration
//       after the battle setup.
//   --every-map (slow, ALAMO_FOC_DATA / ALAMO_EAW_DATA; skipped when unset): every retail .ted map (land and space) goes
//     through the TacticalBattle map load (start_map_battle, first battle tick): no crash, no unknown type, every company
//     placement expanded; per-map counts of companies, hangar-off placements, extra behaviours and marker names (t08).
#include "battle_test_fixture.hpp"

#include <array>
#include <chrono>
#include <cstring>
#include <fstream>
#include <functional>

#include "alamo/vfs/vfs.hpp"

namespace {

using namespace battle_fixture;

std::string map1_types(std::string t) {
    const std::string add =
        " <SpecialStructure Name=\"M1_Shield\"><Base_Position>In_Base</Base_Position><Tactical_Health>500</Tactical_Health>"
        "<CategoryMask>Structure</CategoryMask><Behavior>SELECTABLE</Behavior></SpecialStructure>\n"
        " <StarBase Name=\"M1_Star_Base_E\"><Variant_Of_Existing_Type>S10_Star_Base_1</Variant_Of_Existing_Type>"
        "<Affiliation>Empire</Affiliation><Behavior>DUMMY_STAR_BASE, SELECTABLE</Behavior></StarBase>\n"
        " <SpecialStructure Name=\"M1_Hangar\"><Tactical_Health>500</Tactical_Health><CategoryMask>Structure</CategoryMask>"
        "<Behavior>SELECTABLE, SPAWN_SQUADRON</Behavior><Starting_Spawned_Units_Tech_0>S10_Trooper, 1</Starting_Spawned_Units_Tech_0>"
        "</SpecialStructure>\n"
        " <SpecialStructure Name=\"M1_Bone_Hangar\"><Tactical_Health>500</Tactical_Health><CategoryMask>Structure</CategoryMask>"
        "<Land_Model_Name>M1_SpawnBone.alo</Land_Model_Name><Behavior>SELECTABLE, SPAWN_SQUADRON</Behavior>"
        "<Starting_Spawned_Units_Tech_0>S10_Trooper, 1</Starting_Spawned_Units_Tech_0>"
        "<Spawned_Squadron_Location_Bones>SPAWN_00</Spawned_Squadron_Location_Bones>"
        "<Spawned_Squadron_Location_Flyout_Distances>100.0</Spawned_Squadron_Location_Flyout_Distances></SpecialStructure>\n"
        " <SpecialStructure Name=\"M1_Bone_Garrison\"><Variant_Of_Existing_Type>M1_Bone_Hangar</Variant_Of_Existing_Type>"
        "<Spawn_Garrison_On_Load>True</Spawn_Garrison_On_Load></SpecialStructure>\n"
        " <SpecialStructure Name=\"M1_Lobber\"><Tactical_Health>500</Tactical_Health><CategoryMask>Structure</CategoryMask>"
        "<Behavior>SELECTABLE</Behavior><LandBehavior>LOBBING_SUPERWEAPON</LandBehavior></SpecialStructure>\n"
        " <SpaceUnit Name=\"M1_Escorted\"><Variant_Of_Existing_Type>S10_Frigate</Variant_Of_Existing_Type>"
        "<Space_Escort_Unit_Types>S10_Corvette, S10_Corvette</Space_Escort_Unit_Types></SpaceUnit>\n";
    t.insert(t.rfind("</S10>"), add);
    return t;
}

terrain::ObjectPlacement place(const char* type, float x, float y, uint32_t owner, uint32_t object_id = 0) {
    terrain::ObjectPlacement p;
    p.type_crc = name_crc(type);
    p.owner = owner;
    p.object_id = object_id;
    p.position[0] = x;
    p.position[1] = y;
    return p;
}

std::vector<objects::ObjectHandle> galactic_of(const galactic::GalacticSim& gs, uint32_t type) {
    std::vector<objects::ObjectHandle> out;
    for (const objects::ObjectHandle h : gs.store().all_objects())
        if (gs.store().alive(h) && static_cast<uint32_t>(gs.store().type(h)) == type) out.push_back(h);
    return out;
}

uint32_t count_of(const objects::ObjectStore& st, uint32_t type, int32_t owner) {
    uint32_t n = 0;
    for (const objects::ObjectHandle h : st.all_objects())
        if (st.alive(h) && static_cast<uint32_t>(st.type(h)) == type && st.owner(h) == owner) ++n;
    return n;
}

// One map battle on `map` (start_map_battle), ticked until the battle's first frame; `check` sees the running battle.
template <class Check>
void run_map_battle(const Data& d, bool land, const terrain::MapData& map, galactic::GalacticSetup setup,
                    const std::function<void(galactic::GalacticSim&, galactic::BattleRequest&)>& prepare, const Check& check) {
    jobs::JobSystem js(jobs::JobSystemConfig{0});
    SimConfig cfg;
    cfg.seed = 2026;
    cfg.players = 4;
    SimHost host(d.types, js, cfg);
    galactic::GalacticSim* gs = host.start_galactic(d.rules, setup);
    CHECK(gs != nullptr);
    if (!gs) return;
    CampaignBattleOptions opt;
    opt.battle.max_frames = 0;
    CampaignBattles battles(host, d.crules, *d.catalog,
                            [map](const std::string& name, terrain::MapData& m) {
                                if (name != "M1_Map") return false;
                                m = map;
                                return true;
                            },
                            opt);
    CHECK(battles.attach(nullptr));
    galactic::BattleRequest r;
    r.info.kind = land ? galactic::ConflictKind::Land : galactic::ConflictKind::Space;
    r.info.planet = 1;
    r.info.defender = 0;
    r.info.attacker = 1;
    r.info.map = r.map = "M1_Map";
    prepare(*gs, r);
    CHECK(battles.start_map_battle(std::move(r)));
    for (uint64_t t = 0; t < 40; ++t) {
        host.tick_once();
        if (battles.battle() && host.tactical() && battles.battle()->frame() >= 1) {
            check(*gs, *battles.battle(), *host.tactical());
            return;
        }
    }
    CHECK(false);  // the battle never ran
}

void test_land(const Data& d) {
    const uint32_t trooper = type_of(d, "S10_Trooper"), shield = type_of(d, "M1_Shield");
    static const objects::BehaviorTypeId kHint = objects::behavior_type_id("HINT");
    terrain::MapData m = land_map();
    m.objects.push_back(place("Defending Forces Position", 600.0f, 600.0f, 0, 13));  // #70 t07: never matched by pass 1
    m.objects.push_back(place("Attacker Entry Position", 200.0f, 200.0f, 1));
    // #70 t07: an out-base marker with the in-base marker's id, first in map order: pass 1 still takes the in-base one
    m.objects.push_back(place("Out Base Special Structure Position", 1500.0f, 400.0f, 0, 12));
    m.objects.push_back(place("In Base Special Structure Position", 500.0f, 400.0f, 0, 11));
    m.objects.push_back(place("In Base Special Structure Position", 900.0f, 400.0f, 0, 12));
    terrain::ObjectPlacement squad = place("S10_Squad", 400.0f, 800.0f, 0);  // t03: a company with a hint
    squad.hint = "squadhint";
    terrain::ObjectPlacement::ExtraBehavior e;
    e.name_crc = name_crc("HINT");
    squad.extra_behaviors.push_back(e);  // the company object has HINT (its hint pack), as an editor-written hint implies
    m.objects.push_back(squad);
    terrain::ObjectPlacement one = place("S10_Trooper", 450.0f, 850.0f, 0);  // t04 / t05: extra HINT, marker name, byte 0
    one.extra_behaviors.push_back(e);
    one.has_marker_name = true;
    one.marker_name = "inf4";  // #70 t08: no MARKER, so no marker data pack: the name is not kept
    one.placement_raw.push_back(terrain::RawMini{0x29, {0}});
    m.objects.push_back(one);
    terrain::ObjectPlacement two = place("S10_Trooper", 470.0f, 870.0f, 0);  // #70 t08: HINT and MARKER from 0x4B7: kept
    two.extra_behaviors.push_back(e);
    terrain::ObjectPlacement::ExtraBehavior mk;
    mk.name_crc = name_crc("MARKER");
    two.extra_behaviors.push_back(mk);
    two.has_marker_name = true;
    two.marker_name = "inf5";
    m.objects.push_back(two);
    terrain::ObjectPlacement hangar = place("M1_Hangar", 1100.0f, 1100.0f, 0);  // t05: a hangar placement with byte 0
    hangar.placement_raw.push_back(terrain::RawMini{0x29, {0}});
    m.objects.push_back(hangar);
    m.objects.push_back(place("M1_Hangar", 1300.0f, 1100.0f, 0));  // t05: the same without the byte (creation value: on)
    galactic::GalacticSetup setup = make_setup(d, true);
    setup.forces.push_back({0, 1, shield});
    for (const int32_t remembered : {-1, 12, 13}) {
        objects::ObjectId shield_gid = objects::kNoObjectId;
        run_map_battle(
            d, true, m, setup,
            [&](galactic::GalacticSim& gs, galactic::BattleRequest& r) {
                const auto sh = galactic_of(gs, shield);
                CHECK(sh.size() == 1);
                if (sh.empty()) return;
                r.structures = sh;
                shield_gid = gs.store().id(sh[0]);
                if (remembered >= 0) gs.set_structure_slot(shield_gid, 1, remembered);
            },
            [&](galactic::GalacticSim& gs, TacticalBattle& b, TacticalMode& tm) {
                const objects::ObjectStore& st = tm.primary();
                const MapLoadStats& s = b.map_load_stats();
                std::printf("  land (remembered %d): placements %u, companies %u (%u members), extra behaviours %u, marker names %u "
                            "(%u skipped), hangar off %u, unknown %u, remembered markers %u\n",
                            remembered, s.placements, s.companies, s.company_members, s.extra_behaviors, s.marker_names,
                            s.marker_names_skipped, s.hangar_off, s.unknown_types, s.remembered_markers);
                CHECK(s.companies == 1 && s.company_members == 4 && s.extra_behaviors == 3 && s.marker_names == 1 &&
                      s.marker_names_skipped == 1 && s.hangar_off == 1 && s.unknown_types == 0);
                // t03: four troopers stand at the squad's placement with its hint and HINT; t04: the single one has HINT and the name
                uint32_t members = 0, hinted = 0, named = 0, unnamed = 0;
                for (const objects::ObjectHandle h : st.all_objects()) {
                    if (!st.alive(h) || static_cast<uint32_t>(st.type(h)) != trooper || st.owner(h) != 0) continue;
                    const Vec3 p = st.position(h);
                    const objects::ObjectId id = st.id(h);
                    if (std::fabs(p.x - 400.0f) < 10.0f && std::fabs(p.y - 800.0f) < 10.0f) {  // created at the placement; the first frame separates them
                        ++members;
                        hinted += b.map_hint(id) == "squadhint" && st.has_behavior(h, kHint) ? 1u : 0u;
                    }
                    if (p.x == 450.0f && p.y == 850.0f) unnamed += b.map_marker_name(id).empty() && st.has_behavior(h, kHint) ? 1u : 0u;
                    if (p.x == 470.0f && p.y == 870.0f) named += b.map_marker_name(id) == "inf5" && st.has_behavior(h, kHint) ? 1u : 0u;
                    if (std::getenv("MAP1_DEBUG"))
                        std::printf("    trooper %d at (%.2f, %.2f) hint '%s' HINT %d name '%s'\n", id, static_cast<double>(p.x),
                                    static_cast<double>(p.y), std::string(b.map_hint(id)).c_str(), st.has_behavior(h, kHint) ? 1 : 0,
                                    std::string(b.map_marker_name(id)).c_str());
                }
                CHECK(members == 4 && hinted == 4 && named == 1 && unnamed == 1);
                // q6 t31 (Q6_T30 §1-§2 [H]): the company object itself is not findable: it is removed after expansion
                const uint32_t squad_type = type_of(d, "S10_Squad");
                uint32_t squads = 0;
                for (const objects::ObjectHandle h : st.all_objects())
                    squads += st.exists(h) && static_cast<uint32_t>(st.type(h)) == squad_type ? 1u : 0u;
                CHECK(squads == 0);
                // t05: the byte-0 hangar spawns nothing (+0x3CB off); the one without the byte spawns
                const uint32_t hangar_type = type_of(d, "M1_Hangar");
                uint32_t hangars_off = 0, hangars_on = 0;
                for (const objects::ObjectHandle h : st.all_objects()) {
                    if (!st.alive(h) || static_cast<uint32_t>(st.type(h)) != hangar_type) continue;
                    const bool on = b.hangars() && b.hangars()->spawn_enabled(st.id(h));
                    const float x = st.position(h).x;
                    hangars_off += x == 1100.0f && !on ? 1u : 0u;
                    hangars_on += x == 1300.0f && on ? 1u : 0u;
                }
                CHECK(hangars_off == 1 && hangars_on == 1);
                // t06: the structure on the remembered marker (pass 1), else the first free in-base marker; the memory follows.
                // #70 t07 (0x2a1eb0 [H]): pass 1 searches the in-base and out-base markers only, so a memory of the Defending
                // Forces marker (13) is not matched and the structure takes the first free in-base marker.
                Vec3 at{};
                CHECK(count_of(st, shield, 0) == 1);
                for (const objects::ObjectHandle h : st.all_objects())
                    if (st.alive(h) && static_cast<uint32_t>(st.type(h)) == shield) at = st.position(h);
                const galactic::GalacticSim::StructureSlot* mem = gs.structure_slot(shield_gid);
                CHECK(mem != nullptr);
                const bool matched = remembered == 12;  // 13 is a Defending Forces marker: not matched
                if (!matched) {
                    CHECK(at.x == 500.0f && at.y == 400.0f && s.remembered_markers == 0 && mem && mem->marker == 11 && mem->slot == 0);
                } else {
                    CHECK(at.x == 900.0f && at.y == 400.0f && s.remembered_markers == 1 && mem && mem->marker == 12 && mem->slot == 1);
                }
            });
    }
}

// #70 t07 (0x2a1eb0 [H]): pass 1 reads no used flag, so two structures that remember the same in-base marker both stand
// on it (the second does not fall over to the next free marker).
void test_land_shared_memory(const Data& d) {
    const uint32_t shield = type_of(d, "M1_Shield");
    terrain::MapData m = land_map();
    m.objects.push_back(place("Defending Forces Position", 600.0f, 600.0f, 0));
    m.objects.push_back(place("Attacker Entry Position", 200.0f, 200.0f, 1));
    m.objects.push_back(place("In Base Special Structure Position", 500.0f, 400.0f, 0, 11));
    m.objects.push_back(place("In Base Special Structure Position", 900.0f, 400.0f, 0, 12));
    galactic::GalacticSetup setup = make_setup(d, true);
    setup.forces.push_back({0, 1, shield});
    setup.forces.push_back({0, 1, shield});
    run_map_battle(
        d, true, m, setup,
        [&](galactic::GalacticSim& gs, galactic::BattleRequest& r) {
            const auto sh = galactic_of(gs, shield);
            CHECK(sh.size() == 2);
            r.structures = sh;
            for (const objects::ObjectHandle h : sh) gs.set_structure_slot(gs.store().id(h), 1, 12);
        },
        [&](galactic::GalacticSim&, TacticalBattle& b, TacticalMode& tm) {
            const objects::ObjectStore& st = tm.primary();
            uint32_t on_12 = 0, n = 0;
            for (const objects::ObjectHandle h : st.all_objects()) {
                if (!st.alive(h) || static_cast<uint32_t>(st.type(h)) != shield) continue;
                ++n;
                on_12 += st.position(h).x == 900.0f && st.position(h).y == 400.0f ? 1u : 0u;
            }
            std::printf("  shared memory: %u shields, %u on the remembered marker, remembered markers %u\n", n, on_12,
                        b.map_load_stats().remembered_markers);
            CHECK(n == 2 && on_12 == 2 && b.map_load_stats().remembered_markers == 2);
        });
}

void test_space(const Data& d) {
    const uint32_t fr = type_of(d, "S10_Frigate"), co = type_of(d, "S10_Corvette"), sb = type_of(d, "S10_Star_Base_1"),
                   picked = type_of(d, "M1_Star_Base_E"), esc = type_of(d, "M1_Escorted");
    terrain::MapData m = space_map();
    m.objects.push_back(place("Space Station Position", 1000.0f, 0.0f, 0));
    m.objects.push_back(place("Defending Forces Position", 800.0f, 0.0f, 0));
    m.objects.push_back(place("Attacker Entry Position", -2000.0f, 0.0f, 1));
    galactic::GalacticSetup setup = make_setup(d, false);
    setup.forces = {{0, 1, esc}, {0, 1, sb}, {1, 2, fr}};
    run_map_battle(
        d, false, m, setup,
        [&](galactic::GalacticSim& gs, galactic::BattleRequest& r) {
            const auto s0 = galactic_of(gs, esc), sbase = galactic_of(gs, sb), s1 = galactic_of(gs, fr);
            CHECK(s0.size() == 1 && sbase.size() == 1 && s1.size() == 1);
            galactic::BattleForces f0, f1;
            f0.player = 0;
            f0.start = s0;
            f1.player = 1;
            f1.start = s1;
            r.forces = {f0, f1};
            if (!sbase.empty()) r.starbase = sbase[0];
            r.base_level = 1;
        },
        [&](galactic::GalacticSim&, TacticalBattle& b, TacticalMode& tm) {
            const objects::ObjectStore& st = tm.primary();
            const MapLoadStats& s = b.map_load_stats();
            Vec3 focus{}, other{};
            const bool f0 = b.start_camera_focus(0, focus), f1 = b.start_camera_focus(1, other);
            std::printf("  space: starbase picked %d (M1_Star_Base_E %u, S10_Star_Base_1 %u), escorts %u (corvettes of the defender %u), "
                        "focus %d (%.0f, %.0f), attacker focus %d\n",
                        s.starbase_picked ? 1 : 0, count_of(st, picked, 0), count_of(st, sb, 0), s.escorts, count_of(st, co, 0), f0 ? 1 : 0,
                        static_cast<double>(focus.x), static_cast<double>(focus.y), f1 ? 1 : 0);
            CHECK(s.starbase_picked && count_of(st, picked, 0) == 1 && count_of(st, sb, 0) == 0);
            CHECK(s.escorts == 2 && count_of(st, co, 0) == 2 && count_of(st, esc, 0) == 1);
            CHECK(f0 && focus.x == 1000.0f && focus.y == 0.0f && !f1);
            // #72 t01: the host's start view looks at that focus for the local defender; the attacker (no focus) looks at
            // its own battle units (the frigate, its one unit).
            const Vec3 look0 = b.start_look_at(tm, 0), look1 = b.start_look_at(tm, 1);
            Vec3 frigate{};
            for (const objects::ObjectHandle h : st.all_objects())
                if (st.alive(h) && st.owner(h) == 1 && st.type(h) == fr) frigate = st.position(h);
            std::printf("  start look-at: defender (%.0f, %.0f), attacker (%.0f, %.0f), frigate (%.0f, %.0f)\n",
                        static_cast<double>(look0.x), static_cast<double>(look0.y), static_cast<double>(look1.x),
                        static_cast<double>(look1.y), static_cast<double>(frigate.x), static_cast<double>(frigate.y));
            CHECK(look0.x == 1000.0f && look0.y == 0.0f && look0.z == 0.0f);
            CHECK(look1.x == frigate.x && look1.y == frigate.y && look1.z == 0.0f);
        });
}

// ---- t08: every retail map through the TacticalBattle map load ----

struct EveryMapTotals {
    uint32_t maps = 0, land = 0, space = 0, failed = 0, placements = 0, unknown = 0, unknown_maps = 0, companies = 0, members = 0;
    uint32_t hangar_off = 0, extra = 0, names = 0, names_skipped = 0, picks = 0, allowed = 0, hinted = 0, hinted_no_beh = 0,
             hints_skipped = 0;
};

// Placements whose type is in no shipped XML or script of FoC or EaW (searched 2026-11-17: the CRCs match no name in the
// extracted data text), so retail cannot create them either: pre-release (_PM*), benchmark / testbed maps, the Alderaan
// skirmish demo maps and four types on UM08 (10 placements). Map -> the number of such placements (both data sets).
uint32_t unknown_allowance(std::string_view map) {
    static const std::pair<const char*, uint32_t> kAllow[] = {
        {"BENCHMARK_SPACE", 3}, {"SKIRMISH_DEMO_LAND_ALDERAAN", 1}, {"SKIRMISH_LAND_ALDERAAN_(SHIELD)", 1}, {"SPACE_UNIT_TESTBED", 3},
        {"UM08_REBEL_EVICTION", 10}, {"_PM1_REBEL_SPACE_PLANET_DANTOOINE_", 1}, {"_PM2_EMPIRE_SPACE_PLANET_FONDOR_", 1},
        {"_PM6_EMPIRE_LAND_PLANET_ILUM_", 1}, {"_PM9_EMPIRE_LAND_PLANET_ANAXES_", 15}};
    for (const auto& [n, c] : kAllow)
        if (ascii_iequals(map, n)) return c;
    return 0;
}

void every_map(const char* label, vfs::VfsConfig vc, EveryMapTotals& tot) {
    std::string err;
    auto files = vfs::Vfs::create(vc, &err);
    CHECK(files != nullptr);
    if (!files) return;
    auto d = std::make_unique<Data>();
    std::vector<std::string> diag;
    CHECK(d->db.load(*files, {}, &diag) && d->raw.load(*files) && d->types.build(d->raw, files.get(), &diag));
    CHECK(galactic::load_galactic_rules(*files, d->db, d->types, d->rules, &diag));
    CHECK(galactic::load_conflict_rules(*files, d->db, d->types, d->rules, d->crules, &diag));
    d->catalog = std::make_unique<BattleCatalog>(d->types, files.get(), &d->db.constants(), &diag);
    // The campaign with the most players (every faction a player, so map owners resolve as in a campaign battle).
    galactic::GalacticSetup setup;
    std::string campaign;
    for (const data::CampaignDef& c : d->db.campaigns()) {
        std::vector<std::string> cd;
        galactic::GalacticSetup s = galactic::build_campaign_setup(d->db, d->rules, c.name, {}, &cd);
        if (s.map.planets.size() > 0 && s.players.size() > setup.players.size()) {
            setup = std::move(s);
            campaign = c.name;
        }
    }
    CHECK(setup.players.size() >= 3);
    if (setup.players.size() < 3) return;
    const auto maps = files->list("Data\\Art\\Maps\\*.ted");
    std::printf("  %s: %zu maps, campaign %s (%zu players)\n", label, maps.size(), campaign.c_str(), setup.players.size());
    const auto t0 = std::chrono::steady_clock::now();
    for (const std::string& path : maps) {
        std::string name = path.substr(path.find_last_of("\\/") + 1);
        name = name.substr(0, name.size() - 4);
        terrain::MapData map;
        if (!terrain::load_map(*files, name, map, &err)) {
            std::printf("FAIL %s %s: %s\n", label, name.c_str(), err.c_str());
            ++tot.failed;
            ++g_fail;
            continue;
        }
        const bool land = !map.is_space();
        bool ran = false;
        jobs::JobSystem js(jobs::JobSystemConfig{0});
        SimConfig cfg;
        cfg.seed = 2026;
        cfg.players = session_players(setup.players.size());
        SimHost host(d->types, js, cfg);
        galactic::GalacticSim* gs = host.start_galactic(d->rules, setup);
        CHECK(gs != nullptr);
        if (!gs) return;
        CampaignBattleOptions opt;
        opt.battle.max_frames = 0;
        CampaignBattles battles(host, d->crules, *d->catalog,
                                [&map](const std::string&, terrain::MapData& m) {
                                    m = map;
                                    return true;
                                },
                                opt);
        CHECK(battles.attach(nullptr));
        galactic::BattleRequest r;
        r.info.kind = land ? galactic::ConflictKind::Land : galactic::ConflictKind::Space;
        r.info.planet = 0;
        r.info.defender = 0;
        r.info.attacker = 1;
        r.info.map = r.map = name;
        r.base_level = 5;  // [port] every map object (no base-level drop)
        CHECK(battles.start_map_battle(std::move(r)));
        for (uint64_t t = 0; t < 40 && !ran; ++t) {
            host.tick_once();
            const TacticalBattle* b = battles.battle();
            if (!b || b->frame() < 1) continue;
            ran = true;
            const MapLoadStats& s = b->map_load_stats();
            ++tot.maps;
            tot.land += land ? 1u : 0u;
            tot.space += land ? 0u : 1u;
            tot.placements += s.placements;
            tot.unknown += s.unknown_types;
            const uint32_t allow = unknown_allowance(name);
            if (s.unknown_types > allow) {
                std::printf("FAIL %s %s: %u unknown types (allowance %u)\n", label, name.c_str(), s.unknown_types, allow);
                ++g_fail;
            }
            tot.allowed += std::min(s.unknown_types, allow);
            tot.unknown_maps += s.unknown_types ? 1u : 0u;
            tot.companies += s.companies;
            tot.members += s.company_members;
            tot.hangar_off += s.hangar_off;
            tot.extra += s.extra_behaviors;
            tot.names += s.marker_names;
            tot.names_skipped += s.marker_names_skipped;
            tot.picks += s.spawn_table_picks;
            if (s.unknown_types || s.companies || s.hangar_off)
                std::printf("    %s %s: %u placements, unknown %u, companies %u (%u members), hangar off %u, HINT %u, names %u\n", label,
                            name.c_str(), s.placements, s.unknown_types, s.companies, s.company_members, s.hangar_off, s.extra_behaviors,
                            s.marker_names);
            // Find_Hint walks HINT objects only (Q6_T30 §2 [H]) and the reader keeps a 0x482 hint only on an object
            // with HINT (§3), so every object that got a map hint has HINT; the skipped hints are counted (FoC: 2, on
            // _SPACE_PLANET_AETENII_PIRACY: a Star_Destroyer and a Calamari_Cruiser hinted "pointguard").
            tot.hints_skipped += s.hints_skipped;
            if (const TacticalMode* tm = host.tactical()) {
                static const objects::BehaviorTypeId kHint = objects::behavior_type_id("HINT");
                uint32_t bad = 0;
                for (const objects::ObjectHandle h : tm->primary().all_objects()) {
                    if (!tm->primary().exists(h) || b->map_hint(tm->primary().id(h)).empty()) continue;
                    ++tot.hinted;
                    if (tm->primary().has_behavior(h, kHint)) continue;
                    ++bad;
                    const objects::TypeIndex ty = tm->primary().type(h);
                    std::printf("    %s %s: hint '%s' on %s without HINT\n", label, name.c_str(),
                                std::string(b->map_hint(tm->primary().id(h))).c_str(),
                                static_cast<size_t>(ty) < d->types.size() ? std::string(d->types.raw().types().name[static_cast<size_t>(ty)]).c_str() : "?");
                }
                tot.hinted_no_beh += bad;
                if (bad) {
                    std::printf("FAIL %s %s: %u objects with a map hint but no HINT\n", label, name.c_str(), bad);
                    ++g_fail;
                }
            }
            if (s.companies && s.company_members < s.companies) {  // a company that spawned nothing is a dropped placement
                std::printf("FAIL %s %s: %u companies spawned %u members\n", label, name.c_str(), s.companies, s.company_members);
                ++g_fail;
            }
        }
        if (!ran) {
            std::printf("FAIL %s %s: no battle frame\n", label, name.c_str());
            ++tot.failed;
            ++g_fail;
        }
    }
    std::printf("  %s: %.1f s\n", label, std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count());
}

int run_every_map() {
    const std::string foc = alamo::test::env_or_empty("ALAMO_FOC_DATA"), eaw = alamo::test::env_or_empty("ALAMO_EAW_DATA");
    if (foc.empty() && eaw.empty()) {
        std::printf("every map: ALAMO_FOC_DATA / ALAMO_EAW_DATA not set, skipped\n");
        return 0;
    }
    EveryMapTotals tot;
    if (!foc.empty()) {  // FoC (its EaW base layer is mounted too, as the game does)
        vfs::VfsConfig vc;
        vc.foc_root = foc;
        every_map("FoC", vc, tot);
    }
    if (!eaw.empty()) {  // EaW alone: its own data and maps
        vfs::VfsConfig vc;
        vc.foc_root = eaw;
        vc.eaw_root = std::string();
        every_map("EaW", vc, tot);
    }
    std::printf("every map: %u maps (%u land, %u space), %u failed; %u placements, %u unknown types in %u maps, %u companies -> %u "
                "members, %u hangar-off placements, %u extra behaviours, %u marker names (%u skipped: no MARKER), %u spawn-table "
                "picks, %u hinted objects (%u without HINT), %u hints skipped (no HINT)\n",
                tot.maps, tot.land, tot.space, tot.failed, tot.placements, tot.unknown, tot.unknown_maps, tot.companies, tot.members,
                tot.hangar_off, tot.extra, tot.names, tot.names_skipped, tot.picks, tot.hinted, tot.hinted_no_beh, tot.hints_skipped);
    CHECK(tot.maps > 300 && tot.failed == 0 && tot.unknown == tot.allowed);
    CHECK(tot.names_skipped == 0);  // #70 t08: every shipped 0x46E name (36) is on an object with MARKER
    std::printf(g_fail ? "alamo_sim_map1_tests --every-map: %d failure(s)\n" : "alamo_sim_map1_tests --every-map: OK\n", g_fail);
    return g_fail ? 1 : 0;
}

// #57 t05 (L1_S4_REMAINING special weapons; the map1 t03 follow-up): a map object that is a special weapon is
// registered with the battle's SpecialWeaponSystem when the map load deploys it (l1_register), and that registration is
// still there once the battle runs: the system is created before the map load, not again after it.
void test_land_special_weapon(const Data& d) {
    const uint32_t lobber_t = type_of(d, "M1_Lobber");
    terrain::MapData m = land_map();
    m.objects.push_back(place("Defending Forces Position", 600.0f, 600.0f, 0));
    m.objects.push_back(place("Attacker Entry Position", 200.0f, 200.0f, 1));
    m.objects.push_back(place("M1_Lobber", 900.0f, 900.0f, 0));
    run_map_battle(
        d, true, m, make_setup(d, true), [](galactic::GalacticSim&, galactic::BattleRequest&) {},
        [&](galactic::GalacticSim&, TacticalBattle& b, TacticalMode& tm) {
            const objects::ObjectStore& st = tm.primary();
            objects::ObjectId lobber = objects::kNoObjectId;
            for (const objects::ObjectHandle h : st.all_objects())
                if (st.alive(h) && static_cast<uint32_t>(st.type(h)) == lobber_t) lobber = st.id(h);
            const bool registered = lobber != objects::kNoObjectId && b.special_weapons().has(lobber);
            std::printf("  map lobber %d: registered %d\n", lobber, registered ? 1 : 0);
            CHECK(lobber != objects::kNoObjectId && registered);
        });
}

// g_data_extra: M1_Bone_Hangar's model, a root and SPAWN_00 (bind matrix rows: x axis column (0.6, -0.8, 0), at (0, -30, 0))
void write_spawn_bone_model(const fs::path& root) {
    const std::vector<uint8_t> alo = skeleton_alo({{"Root", {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0}},
                                                   {"SPAWN_00", {0.6f, 0.8f, 0, 0, -0.8f, 0.6f, 0, -30.0f, 0, 0, 1, 0}}});
    std::error_code ec;
    fs::create_directories(root / "Data/Art/Models", ec);
    std::ofstream f(root / "Data/Art/Models/M1_SpawnBone.alo", std::ios::binary | std::ios::trunc);
    f.write(reinterpret_cast<const char*>(alo.data()), static_cast<std::streamsize>(alo.size()));
}

// #44 t02 (R3G_ANIM_TIMED_RE §3 [H]; 0x594dc0 and 0x593960 read 2026-11-23): with Spawned_Squadron_Location_Bones the
// spawn facing is the bone's world x axis (matrix column 0) and the fly-out point is spawn point + that facing x the bone's
// fly-out distance. M1_Bone_Hangar faces +x (map yaw 90); its SPAWN_00 bone sits 30 in front of the nose (model
// (0, -30, 0); ALO model space: nose -Y, left +X) with its x axis at model (0.6, -0.8): 0.8 forward and 0.6 to the left.
// In the world: spawn point = hangar + (30, 0), facing (0.8, 0.6), fly-out point hangar + (110, 60); along the structure's
// facing (the port before t02) it was hangar + (130, 0).
//   * the launch (0x594dc0, `garrison` false): the unit starts at the spawn point and is ordered to the fly-out point;
//   * Spawn_Garrison_On_Load (0x593960, M1_Bone_Garrison): the unit is created at the fly-out point itself, facing along
//     the bone, with no order.
void test_land_spawn_bone(const Data& d, bool garrison) {
    const uint32_t trooper = type_of(d, "S10_Trooper"), bone_hangar = type_of(d, garrison ? "M1_Bone_Garrison" : "M1_Bone_Hangar");
    terrain::MapData m = land_map();
    m.objects.push_back(place("Defending Forces Position", 600.0f, 600.0f, 0));
    m.objects.push_back(place("Attacker Entry Position", 200.0f, 200.0f, 1));
    terrain::ObjectPlacement hp = place(garrison ? "M1_Bone_Garrison" : "M1_Bone_Hangar", 1000.0f, 1000.0f, 0);
    hp.rotation_deg[2] = 90.0f;  // the map yaw: 0 faces -y, 90 faces +x
    m.objects.push_back(hp);
    // the battle as run_map_battle, ticked until the hangar's trooper shows up (the launch: ordered; the garrison: at the
    // fly-out point)
    jobs::JobSystem js(jobs::JobSystemConfig{0});
    SimConfig cfg;
    cfg.seed = 2026;
    cfg.players = 4;
    SimHost host(d.types, js, cfg);
    galactic::GalacticSim* gs = host.start_galactic(d.rules, make_setup(d, true));
    CHECK(gs != nullptr);
    if (!gs) return;
    CampaignBattleOptions opt;
    opt.battle.max_frames = 0;
    CampaignBattles battles(host, d.crules, *d.catalog,
                            [m](const std::string& name, terrain::MapData& out) {
                                if (name != "M1_Map") return false;
                                out = m;
                                return true;
                            },
                            opt);
    CHECK(battles.attach(nullptr));
    galactic::BattleRequest r;
    r.info.kind = galactic::ConflictKind::Land;
    r.info.planet = 1;
    r.info.defender = 0;
    r.info.attacker = 1;
    r.info.map = r.map = "M1_Map";
    CHECK(battles.start_map_battle(std::move(r)));
    uint32_t hangars = 0, found = 0, right = 0;
    for (uint64_t t = 0; t < 200 && found == 0; ++t) {
        host.tick_once();
        TacticalBattle* b = battles.battle();
        TacticalMode* tm = host.tactical();
        if (!b || !tm || b->frame() < 1) continue;
        const objects::ObjectStore& st = tm->primary();
        Vec3 at{}, hf{};
        hangars = 0;
        for (const objects::ObjectHandle h : st.all_objects())
            if (st.alive(h) && static_cast<uint32_t>(st.type(h)) == bone_hangar) {
                at = st.position(h);
                hf = st.facing(h);
                ++hangars;
            }
        for (const objects::ObjectHandle h : st.all_objects()) {
            if (!st.alive(h) || static_cast<uint32_t>(st.type(h)) != trooper || st.owner(h) != 0) continue;
            const objects::ObjectId id = st.id(h);
            if (!b->movement().has_unit(id)) continue;
            const tactical::movement::UnitView v = b->movement().view(id);
            const bool near_hangar = std::fabs(v.pos.x - at.x) < 200.0f && std::fabs(v.pos.y - at.y) < 200.0f;
            if (!near_hangar || (!garrison && !v.has_goal)) continue;
            ++found;
            const Vec3 uf = st.facing(h);
            std::printf("  %s at (%.1f, %.1f): trooper %d at (%.2f, %.2f) facing (%.3f, %.3f) goal %d (%.2f, %.2f), frame %u\n",
                        garrison ? "bone garrison" : "bone hangar", static_cast<double>(at.x), static_cast<double>(at.y), id,
                        static_cast<double>(v.pos.x), static_cast<double>(v.pos.y), static_cast<double>(uf.x),
                        static_cast<double>(uf.y), v.has_goal ? 1 : 0, static_cast<double>(v.goal.x), static_cast<double>(v.goal.y),
                        b->frame());
            const bool facing_ok = std::fabs(uf.x - 0.8f) < 0.01f && std::fabs(uf.y - 0.6f) < 0.01f;
            if (garrison) {
                right += hf.x > 0.999f && facing_ok && !v.has_goal && std::fabs(v.pos.x - (at.x + 110.0f)) < 0.5f &&
                                 std::fabs(v.pos.y - (at.y + 60.0f)) < 0.5f
                             ? 1u
                             : 0u;
            } else {
                right += hf.x > 0.999f && std::fabs(v.pos.x - (at.x + 30.0f)) < 0.5f && std::fabs(v.pos.y - at.y) < 0.5f &&
                                 std::fabs(v.goal.x - (at.x + 110.0f)) < 0.5f && std::fabs(v.goal.y - (at.y + 60.0f)) < 0.5f
                             ? 1u
                             : 0u;
            }
        }
    }
    CHECK(hangars == 1 && found == 1 && right == 1);
}

// #44 t01 (STATION_HANGARS_SOUND §2.5 "Land" / §2.7, R3G_ANIM_TIMED_RE §3): M1_Bone_Hangar's cycle through the battle
// host with Structure Open (20 frames), Hold and Close (20 frames) clips at 30 fps. The services run every 30 frames:
// state 1 (opening) at the first service; at the first service where Open has finished the trooper is launched at the
// bone and ordered to the fly-out point (hg_land_launch), state 2; the trooper's move finishing (signal 10, hg_move_done:
// the host calls unit_left that frame, 0x593760) plays Close at once, state 3; the first service at least Close's 20
// frames later finds it finished, state 0. The trooper stays counted (live 1), so nothing launches again.
// #57 t02 (0x594880, index 0xFFFF, loop): Structure Hold (two variants here) is still the spawner's clip, looping, more than
// its 30 frames after the launch, and its variant is one of the two.
void test_land_spawner_cycle(const Data& d) {
#ifdef ALAMO_SIM_HAVE_ANIM
    const uint32_t trooper = type_of(d, "S10_Trooper"), bone_hangar = type_of(d, "M1_Bone_Hangar");
    const auto model = synth_model("M1_SPAWNBONE", {{anim::kAnimIdle, 30u}, {68, 20u}, {69, 30u}, {69, 30u}, {70, 20u}});
    SimAnimation animation(d.types, *d.files);
    animation.set_model(static_cast<objects::TypeIndex>(bone_hangar), objects::ModeKind::Land, model.get());
    terrain::MapData m = land_map();
    m.objects.push_back(place("Defending Forces Position", 600.0f, 600.0f, 0));
    m.objects.push_back(place("Attacker Entry Position", 200.0f, 200.0f, 1));
    terrain::ObjectPlacement hp = place("M1_Bone_Hangar", 1000.0f, 1000.0f, 0);
    hp.rotation_deg[2] = 90.0f;  // faces +x: the fly-out point is hangar + (110, 60) (test_land_spawn_bone)
    m.objects.push_back(hp);
    jobs::JobSystem js(jobs::JobSystemConfig{0});
    SimConfig cfg;
    cfg.seed = 2026;
    cfg.players = 4;
    SimHost host(d.types, js, cfg);
    galactic::GalacticSim* gs = host.start_galactic(d.rules, make_setup(d, true));
    CHECK(gs != nullptr);
    if (!gs) return;
    animation.attach(host, false);
    CampaignBattleOptions opt;
    opt.battle.max_frames = 0;
    opt.animation = &animation;
    CampaignBattles battles(host, d.crules, *d.catalog,
                            [m](const std::string& name, terrain::MapData& out) {
                                if (name != "M1_Map") return false;
                                out = m;
                                return true;
                            },
                            opt);
    CHECK(battles.attach(nullptr));
    galactic::BattleRequest r;
    r.info.kind = galactic::ConflictKind::Land;
    r.info.planet = 1;
    r.info.defender = 0;
    r.info.attacker = 1;
    r.info.map = r.map = "M1_Map";
    CHECK(battles.start_map_battle(std::move(r)));
    std::vector<std::pair<uint32_t, uint8_t>> states;  // (frame, new state)
    objects::ObjectId hangar = objects::kNoObjectId, unit = objects::kNoObjectId;
    std::vector<objects::ObjectId> present;
    uint32_t arrived = 0, launched = 0;
    bool at_flyout = false;
    int32_t live = -1;
    int hold = -1;  // the spawner's clip 40 frames after the launch: 1 Hold looping (variant 0 / 1), 0 anything else
    for (uint64_t t = 0; t < 600; ++t) {
        host.tick_once();
        TacticalBattle* b = battles.battle();
        TacticalMode* tm = host.tactical();
        if (!b || !tm || b->frame() < 1 || !b->hangars()) continue;
        const objects::ObjectStore& st = tm->primary();
        Vec3 at{};
        for (const objects::ObjectHandle h : st.all_objects())
            if (st.alive(h) && static_cast<uint32_t>(st.type(h)) == bone_hangar) {
                hangar = st.id(h);
                at = st.position(h);
            }
        if (hangar == objects::kNoObjectId) continue;
        if (states.empty())  // the troopers already on the map (start forces) are not the hangar's
            for (const objects::ObjectHandle h : st.all_objects())
                if (st.alive(h) && static_cast<uint32_t>(st.type(h)) == trooper) present.push_back(st.id(h));
        const uint8_t s = b->hangars()->state(hangar);
        if (states.empty() || states.back().second != s) states.push_back({b->frame(), s});
        if (unit == objects::kNoObjectId)
            for (const objects::ObjectHandle h : st.all_objects())
                if (st.alive(h) && static_cast<uint32_t>(st.type(h)) == trooper && st.owner(h) == 0 &&
                    std::find(present.begin(), present.end(), st.id(h)) == present.end() &&
                    b->movement().has_unit(st.id(h)) && b->movement().view(st.id(h)).has_goal) {
                    unit = st.id(h);
                    launched = b->frame();
                }
        if (hold < 0 && launched != 0 && b->frame() == launched + 40) {
            const anim::AnimWorld* w = animation.find_world(*tm);
            const anim::SlotView sv = w && w->contains(hangar) ? w->slot(hangar) : anim::SlotView{};
            hold = sv.type == 69 && sv.mode == anim::SlotMode::Loop && sv.variant < 2 ? 1 : 0;
        }
        if (unit != objects::kNoObjectId && arrived == 0 && !b->movement().view(unit).has_goal) {
            arrived = b->frame();
            const tactical::movement::UnitView v = b->movement().view(unit);
            at_flyout = std::fabs(v.pos.x - (at.x + 110.0f)) < 2.0f && std::fabs(v.pos.y - (at.y + 60.0f)) < 2.0f;
        }
        if (states.size() >= 4 && states.back().second == 0) {
            live = b->hangars()->live(hangar, 0);
            break;
        }
    }
    animation.detach(host);
    std::printf("  #44 t01 spawner cycle: states");
    for (const auto& [f, s] : states) std::printf(" %u@%u", static_cast<unsigned>(s), f);
    std::printf("; launched %u, arrived %u (at the fly-out point %d), live %d, Hold looping %d\n", launched, arrived, at_flyout ? 1 : 0,
                live, hold);
    CHECK(states.size() >= 4);
    if (states.size() < 4) return;
    const size_t k = states.size() - 4;  // the last four: 1 opening, 2 out, 3 closing, 0
    CHECK(states[k].second == 1 && states[k + 1].second == 2 && states[k + 2].second == 3 && states[k + 3].second == 0);
    const uint32_t open = states[k].first, out = states[k + 1].first, close = states[k + 2].first, idle = states[k + 3].first;
    CHECK(out - open == 30 && launched == out);                   // Open (20) has finished at the next service
    CHECK(at_flyout && arrived > out && close == arrived);  // signal 10 closes at once
    // the first service (30-frame steps from the first) at least 20 frames after Close started
    CHECK((idle - open) % 30 == 0 && idle - close >= 20 && idle - close < 50 && live == 1);
    CHECK(arrived > launched + 40 && hold == 1);
#else
    (void)d;
    std::printf("  #44 t01 spawner cycle: skipped (no engine/anim)\n");
#endif
}

}  // namespace

int main(int argc, char** argv) {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    for (int i = 1; i < argc; ++i)
        if (std::string(argv[i]) == "--every-map") return run_every_map();
    g_temp_prefix = "alamo_sim_map1_tests";
    g_types_patch = map1_types;
    g_data_extra = write_spawn_bone_model;
    auto d = make_data();
    g_types_patch = nullptr;
    g_data_extra = nullptr;
    std::printf("map1: land map load (companies, record data, hangar byte, structure memory)\n");
    test_land(*d);
    test_land_shared_memory(*d);  // #70 t07
    std::printf("map1: space start (starbase pick, escorts, camera focus)\n");
    test_space(*d);
    std::printf("map1: a map special weapon stays registered (#57 t05)\n");
    test_land_special_weapon(*d);
    std::printf("map1: land spawner fly-out along the spawn bone (#44 t02)\n");
    test_land_spawn_bone(*d, false);
    test_land_spawn_bone(*d, true);
    std::printf("map1: land spawner Open / launch / Close cycle (#44 t01)\n");
    test_land_spawner_cycle(*d);
    std::printf(g_fail ? "alamo_sim_map1_tests: %d failure(s)\n" : "alamo_sim_map1_tests: OK\n", g_fail);
    return g_fail ? 1 : 0;
}
