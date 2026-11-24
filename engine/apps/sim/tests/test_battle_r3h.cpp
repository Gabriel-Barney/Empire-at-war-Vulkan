// alamo_sim_r3h_tests: r3h "Ability, hero and structure clip playback" on a synthetic land battle (battle_test_fixture.hpp,
// no game data). The battle hands the ability system's clips to its animation world (TacticalBattle::oi_ability_anims,
// ABILITIES_HARDPOINTS §4.15): the AnimWorld row type of a trooper with FORCE_SIGHT is traced per frame - Force_Revel_Begin
// (105) one-shot, Force_Revel_Loop (106) from the frame Begin finishes, Force_Revel_End (107) when the 2 s run out, then
// the IDLE behaviour's idle once End has finished.
#include "battle_test_fixture.hpp"
#include "tactical_battle_test_access.hpp"  // ref3 t06
#include "alamo/tactical/abilities/ability_types.hpp"
#include "alamo/tactical/combat/rocket.hpp"  // #357: the wreck's spline (SplineClass)

namespace {


// A land battle on S10_B (invaded by player 1's S10_Squad company from planet 2). `per_tick(b, tm, host)` runs after every
// host tick once the battle exists; it returns false to stop. `animation` is attached (its CRC system too).
template <class F>
void land_battle(Data& d, SimAnimation* animation, uint32_t max_frames, F per_tick) {
    jobs::JobSystem js(jobs::JobSystemConfig{0});
    SimConfig cfg;
    cfg.seed = 2026;
    cfg.players = 4;
    SimHost host(d.types, js, cfg);
    galactic::GalacticSim* gs = host.start_galactic(d.rules, make_setup(d, true));
    CHECK(gs != nullptr);
    if (!gs) return;
    CampaignBattleOptions opt;
    opt.conflict.human_choice = galactic::BattleChoice::Fight;
    opt.battle.max_frames = max_frames;
    if (animation) {
        animation->attach(host, true);
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

std::string sight_patch(std::string t) {
    // The trooper gets FORCE_SIGHT with a 2 s Force_Sight_Ability (as the fog data's troopers).
    const std::string at = "<GroundInfantry Name=\"S10_Trooper\">";
    if (const size_t p = t.find(at); p != std::string::npos)
        t.insert(p + at.size(),
                 "<Unit_Abilities_Data SubObjectList=\"Yes\"><Unit_Ability><Type>FORCE_SIGHT</Type><Recharge_Seconds>50</Recharge_Seconds>"
                 "<GUI_Activated_Ability_Name>S10_Force_Sight</GUI_Activated_Ability_Name></Unit_Ability></Unit_Abilities_Data>"
                 "<Abilities SubObjectList=\"Yes\"><Force_Sight_Ability Name=\"S10_Force_Sight\"><Activation_Style>User_Input</Activation_Style>"
                 "<Force_Sight_Duration_In_Secs>2.0</Force_Sight_Duration_In_Secs></Force_Sight_Ability></Abilities>");
    return t;
}

// ---- t01: Force_Sight's three clips on the AnimWorld row ----
void test_force_sight_rows() {
#ifdef ALAMO_SIM_HAVE_ANIM
    g_temp_prefix = "alamo_sim_r3h_tests_sight";
    g_types_patch = sight_patch;
    auto d = make_data();
    g_types_patch = nullptr;
    const objects::TypeIndex trooper = d->types.resolve_name("S10_Trooper");
    CHECK(trooper != objects::kNone);
    const auto model = synth_model("R3H_TROOPER", {{anim::kAnimIdle, 30u}, {anim::kAnimMove, 20u}, {105, 20u}, {106, 40u}, {107, 12u}});
    SimAnimation animation(d->types, *d->files);
    animation.set_model(trooper, objects::ModeKind::Land, model.get());
    objects::ObjectId unit = objects::kNoObjectId;
    uint32_t on = 0;
    int32_t begin = -1, loop = -1, end = -1, idle = -1, last = -2;
    std::string trace;
    land_battle(*d, &animation, 900, [&](TacticalBattle& b, TacticalMode& tm, SimHost&) {
        objects::ObjectStore& st = tm.primary();
        const anim::AnimWorld* w = animation.find_world(tm);
        if (on == 0) {
            if (b.frame() < 30) return true;
            for (const objects::ObjectHandle h : st.all_objects())
                if (unit == objects::kNoObjectId && st.alive(h) && st.type(h) == trooper && w && w->contains(st.id(h))) unit = st.id(h);
            CHECK(unit != objects::kNoObjectId);
            if (unit == objects::kNoObjectId) return false;
            const objects::ObjectId ids[] = {unit};
            CHECK(alamo::sim::TacticalBattleTestAccess::abilities(b).set_unit_ability_mode(ids, tactical::abilities::kForceSight, true, b.frame()) == 1);
            on = b.frame();
            return true;
        }
        if (!w || !w->contains(unit) || !b.combat().alive(unit)) return false;
        // The row after this frame's tick (the clips the battle played at frame f advance from f + 1).
        const int32_t type = w->current_type(unit);
        const int32_t rel = static_cast<int32_t>(b.frame() - on);
        if (type != last) {
            trace += " +" + std::to_string(rel) + ":" + std::to_string(type);
            last = type;
            if (type == 105 && begin < 0) begin = rel;
            if (type == 106 && loop < 0) loop = rel;
            if (type == 107 && end < 0) end = rel;
            if (end >= 0 && type != 107 && idle < 0) idle = rel;
        }
        return rel < 200 && idle < 0;
    });
    // Begin is played by the activation (between ticks, frame +0; first sampled after the next tick); Begin (20 frames)
    // finishes 18 frames later -> the loop (§4.14, played in the frame the record reports it finished); the 2 s run out
    // int(30 x 2) = 60 updates after the apply -> End (12 frames, blend 0.5), finished 10 frames later; then the unit's
    // own clips again (the locomotor: it still walks).
    std::printf("  r3h force sight rows (frame:type):%s\n", trace.c_str());
    std::printf("  r3h force sight: begin +%d, loop +%d, end +%d, next clip +%d\n", begin, loop, end, idle);
    CHECK(begin == 1 && loop == 18 && end == 60 && idle == 71);
#else
    std::printf("  r3h force sight rows: skipped (no engine/anim)\n");
#endif
}

// r3h review S2-2: a trooper killed by a lethal script Take_Damage while its Force_Sight loop runs - the dying removes
// the special (Force_Revel_End queued) and the death clip starts; no ability clip is played over the death clip / corpse.
// Without a Die clip the row stays as an unlocked corpse (no death clip locks it): the queued End must not play on it.
void test_force_sight_death(bool die_clip) {
#ifdef ALAMO_SIM_HAVE_ANIM
    g_temp_prefix = die_clip ? "alamo_sim_r3h_tests_death" : "alamo_sim_r3h_tests_corpse";
    g_types_patch = sight_patch;
    auto d = make_data();
    g_types_patch = nullptr;
    const objects::TypeIndex trooper = d->types.resolve_name("S10_Trooper");
    const auto model = die_clip ? synth_model("R3H_TROOPER", {{anim::kAnimIdle, 30u}, {anim::kAnimMove, 20u}, {anim::kAnimDie, 40u},
                                                              {105, 20u}, {106, 40u}, {107, 12u}})
                                : synth_model("R3H_TROOPER", {{anim::kAnimIdle, 30u}, {anim::kAnimMove, 20u}, {105, 20u}, {106, 40u}, {107, 12u}});
    SimAnimation animation(d->types, *d->files);
    animation.set_model(trooper, objects::ModeKind::Land, model.get());
    objects::ObjectId unit = objects::kNoObjectId;
    uint32_t on = 0;
    int32_t last = -2, killed = -1;
    bool death_seen = false, ability_after_death = false;
    std::string trace;
    land_battle(*d, &animation, 900, [&](TacticalBattle& b, TacticalMode& tm, SimHost&) {
        objects::ObjectStore& st = tm.primary();
        const anim::AnimWorld* w = animation.find_world(tm);
        if (on == 0) {
            if (b.frame() < 30) return true;
            for (const objects::ObjectHandle h : st.all_objects())
                if (unit == objects::kNoObjectId && st.alive(h) && st.type(h) == trooper && w && w->contains(st.id(h))) unit = st.id(h);
            if (unit == objects::kNoObjectId) return false;
            const objects::ObjectId ids[] = {unit};
            CHECK(alamo::sim::TacticalBattleTestAccess::abilities(b).set_unit_ability_mode(ids, tactical::abilities::kForceSight, true, b.frame()) == 1);
            on = b.frame();
            return true;
        }
        const int32_t rel = static_cast<int32_t>(b.frame() - on);
        if (!w || !w->contains(unit)) return false;
        const int32_t type = w->current_type(unit);
        if (type != last) {
            trace += " +" + std::to_string(rel) + ":" + std::to_string(type);
            last = type;
        }
        if (killed >= 0 && (type == anim::kAnimDie || !die_clip)) death_seen = true;
        if (death_seen && type >= 105 && type <= 107 && type != 106) ability_after_death = true;
        if (killed < 0 && rel == 30) {  // in the loop: a lethal script Take_Damage between ticks
            CHECK(type == 106);
            CHECK(b.script_damage(tm, unit, 1.0e6f, false));
            killed = rel;
        }
        return rel < 120;
    });
    std::printf("  r3h force sight death (%s) rows (frame:type):%s\n", die_clip ? "Die clip" : "no Die clip", trace.c_str());
    CHECK(killed == 30 && death_seen && !ability_after_death);
#else
    (void)die_clip;
    std::printf("  r3h force sight death: skipped (no engine/anim)\n");
#endif
}

// r3h review (stuck gate): a model with Force_Revel_Begin and _Loop but no _End - when the 2 s run out the closing play
// fails; the record must end the hold so the unit's own clips (the locomotor) take over again instead of the loop
// running for good behind a gate that skips the locomotor / IDLE service.
void test_force_sight_no_end() {
#ifdef ALAMO_SIM_HAVE_ANIM
    g_temp_prefix = "alamo_sim_r3h_tests_noend";
    g_types_patch = sight_patch;
    auto d = make_data();
    g_types_patch = nullptr;
    const objects::TypeIndex trooper = d->types.resolve_name("S10_Trooper");
    const auto model = synth_model("R3H_TROOPER", {{anim::kAnimIdle, 30u}, {anim::kAnimMove, 20u}, {105, 20u}, {106, 40u}});
    SimAnimation animation(d->types, *d->files);
    animation.set_model(trooper, objects::ModeKind::Land, model.get());
    objects::ObjectId unit = objects::kNoObjectId;
    uint32_t on = 0;
    int32_t last = -2, after = -1;
    std::string trace;
    land_battle(*d, &animation, 900, [&](TacticalBattle& b, TacticalMode& tm, SimHost&) {
        objects::ObjectStore& st = tm.primary();
        const anim::AnimWorld* w = animation.find_world(tm);
        if (on == 0) {
            if (b.frame() < 30) return true;
            for (const objects::ObjectHandle h : st.all_objects())
                if (unit == objects::kNoObjectId && st.alive(h) && st.type(h) == trooper && w && w->contains(st.id(h))) unit = st.id(h);
            if (unit == objects::kNoObjectId) return false;
            const objects::ObjectId ids[] = {unit};
            CHECK(alamo::sim::TacticalBattleTestAccess::abilities(b).set_unit_ability_mode(ids, tactical::abilities::kForceSight, true, b.frame()) == 1);
            on = b.frame();
            return true;
        }
        if (!w || !w->contains(unit) || !b.combat().alive(unit)) return false;
        const int32_t type = w->current_type(unit);
        const int32_t rel = static_cast<int32_t>(b.frame() - on);
        if (type != last) {
            trace += " +" + std::to_string(rel) + ":" + std::to_string(type);
            last = type;
            if (rel > 18 && type != 106 && after < 0) after = rel;
        }
        return rel < 200 && after < 0;
    });
    std::printf("  r3h force sight without End rows (frame:type):%s\n", trace.c_str());
    CHECK(after >= 60 && after <= 62);
#else
    std::printf("  r3h force sight without End: skipped (no engine/anim)\n");
#endif
}

// ---- Pad_Sell at a skirmish start without pre-build ----
#ifdef ALAMO_SIM_HAVE_ANIM
std::string pad_patch(std::string t) {
    // An Empire / Underworld build pad whose Multiplayer row lists an under-construction turret (the pre-placed type the
    // port reads), the turret it builds.
    const size_t end = t.find("</S10>");
    if (end != std::string::npos)
        t.insert(end, " <SpecialStructure Name=\"R3H_Pad\"><Behavior>TACTICAL_BUILD_OBJECTS</Behavior>"
                      "<Tactical_Buildable_Objects_Multiplayer>Empire, R3H_UC_Turret, Underworld, R3H_UC_Turret</Tactical_Buildable_Objects_Multiplayer>"
                      "</SpecialStructure>\n"
                      " <SpecialStructure Name=\"R3H_UC_Turret\"><Behavior>TACTICAL_UNDER_CONSTRUCTION</Behavior><Tactical_Health>100</Tactical_Health>"
                      "<Tactical_Build_Time_Seconds>2</Tactical_Build_Time_Seconds><Tactical_Buildable_Constructed>R3H_Turret</Tactical_Buildable_Constructed>"
                      "</SpecialStructure>\n"
                      " <SpecialStructure Name=\"R3H_Turret\"><Tactical_Health>500</Tactical_Health></SpecialStructure>\n");
    return t;
}

// One land skirmish map battle with the pre-built-bases option `prebuild`: the pad's AnimWorld row type and contents
// after the first tick.
void pad_start(bool prebuild, int32_t& type, bool& built) {
    type = -2;
    built = false;
#ifdef ALAMO_SIM_HAVE_ANIM
    g_temp_prefix = prebuild ? "alamo_sim_r3h_tests_pad_pre" : "alamo_sim_r3h_tests_pad";
    g_types_patch = pad_patch;
    auto d = make_data();
    g_types_patch = nullptr;
    const objects::TypeIndex pad_t = d->types.resolve_name("R3H_Pad");
    CHECK(pad_t != objects::kNone);
    const auto model = synth_model("R3H_PAD", {{anim::kAnimIdle, 30u}, {116, 20u}, {117, 20u}});
    SimAnimation animation(d->types, *d->files);
    animation.set_model(pad_t, objects::ModeKind::Land, model.get());
    jobs::JobSystem js(jobs::JobSystemConfig{0});
    SimConfig cfg;
    cfg.seed = 2026;
    cfg.players = 4;
    SimHost host(d->types, js, cfg);
    galactic::GalacticSim* gs = host.start_galactic(d->rules, make_setup(*d, true));
    CHECK(gs != nullptr);
    if (!gs) return;
    CampaignBattleOptions opt;
    opt.battle.max_frames = 0;
    opt.battle.skirmish = true;
    opt.battle.skirmish_map = true;
    opt.battle.skirmish_options.set = true;
    opt.battle.skirmish_options.prebuilt_base = prebuild;
    animation.attach(host, true);
    opt.animation = &animation;
    const uint32_t empire = d->rules.find_faction("Empire");
    CampaignBattles battles(host, d->crules, *d->catalog,
                            [empire](const std::string& name, terrain::MapData& m) {
                                if (name != "S10_Land_B") return false;
                                m = land_map();
                                terrain::ObjectPlacement p;
                                p.type_crc = name_crc("R3H_Pad");
                                p.owner = empire;  // the faction index -> player 0, a skirmish slot
                                p.position[0] = -1500.0f;
                                p.position[1] = -1500.0f;
                                m.objects.push_back(p);
                                return true;
                            },
                            opt);
    CHECK(battles.attach(nullptr));
    galactic::BattleRequest r;
    r.info.kind = galactic::ConflictKind::Land;
    r.info.planet = 1;
    r.info.defender = 0;
    r.info.attacker = 1;
    r.info.map = r.map = "S10_Land_B";
    CHECK(battles.start_map_battle(std::move(r)));
    for (uint64_t t = 0; t < 60; ++t) {
        host.tick_once();
        const TacticalBattle* b = battles.battle();
        TacticalMode* tm = host.tactical();
        if (!b || !tm || b->frame() < 1) continue;
        const objects::ObjectStore& st = tm->primary();
        const anim::AnimWorld* w = animation.find_world(*tm);
        for (const objects::ObjectHandle h : st.all_objects())
            if (st.active(h) && st.type(h) == pad_t) {
                type = w && w->contains(st.id(h)) ? w->current_type(st.id(h)) : -1;
                built = b->pad_contents(st.id(h)) != objects::kNoObjectId;
            }
        break;
    }
    animation.detach(host);
#else
    (void)prebuild;
#endif
}
#endif

void test_pad_sell_start() {
    // tru3 t06 (SKIRMISH_SETUP 2.6, pad vf13 0x42df80 [H]): a skirmish whose pre-built-bases option is off plays Pad_Sell
    // (117) on each empty pad that holds a pre-placed type, from the first tick (the pad's row exists from then on); with
    // the option on the pad builds its structure instead and plays nothing.
#ifdef ALAMO_SIM_HAVE_ANIM
    int32_t type = 0;
    bool built = false;
    pad_start(false, type, built);
    std::printf("  tru3 pad at a start without pre-build: row type %d, built %d\n", type, built ? 1 : 0);
    CHECK(type == 117 && !built);
    pad_start(true, type, built);
    std::printf("  tru3 pad at a pre-built start: row type %d, built %d\n", type, built ? 1 : 0);
    CHECK(type != 117 && built);
#else
    std::printf("  tru3 pad sell: skipped (no engine/anim)\n");
#endif
}

}  // namespace

// #327 t06: every battle death that is not a silent removal is a UnitDied presentation event - a combat tick's and a
// script kill made between ticks alike (that one never reaches CombatSystem::deaths() after a tick, which the audio host
// read before) - with its type, owner, position and death clone.
void test_unit_died_presentation() {
    g_temp_prefix = "alamo_sim_r3h_tests_327t06";
    auto d = make_data();
    const objects::TypeIndex trooper = d->types.resolve_name("S10_Trooper");
    objects::ObjectId victim = objects::kNoObjectId;
    uint32_t kill_frame = 0, died_events = 0, deaths = 0;
    bool victim_event = false, fields_ok = true;
    PresentationEvents pres;
    land_battle(*d, nullptr, 900, [&](TacticalBattle& b, TacticalMode& tm, SimHost&) {
        b.set_emit_presentation(true);
        pres.clear();
        b.drain_presentation(pres);
        const objects::ObjectStore& st = tm.primary();
        for (const PresentationEvent& e : pres) {
            if (e.kind != PresentationEventKind::UnitDied) continue;
            ++died_events;
            fields_ok = fields_ok && e.type >= 0 && e.object != objects::kNoObjectId && e.clone_type >= 0;
            if (e.object == victim) victim_event = e.type == static_cast<int32_t>(trooper) && e.frame == kill_frame;
        }
        deaths = b.stats().deaths;
        if (victim == objects::kNoObjectId && b.frame() >= 60) {
            for (const objects::ObjectHandle h : st.all_objects())
                if (victim == objects::kNoObjectId && st.alive(h) && st.type(h) == trooper) victim = st.id(h);
            if (victim != objects::kNoObjectId) {
                kill_frame = b.frame();  // a kill between ticks belongs to the frame just run
                CHECK(b.script_damage(tm, victim, 1.0e6f, false));
            }
        }
        return b.frame() < 400;
    });
    std::printf("  #327 t06 UnitDied: %u events, %u battle deaths, script kill seen %d\n", died_events, deaths, victim_event ? 1 : 0);
    CHECK(victim != objects::kNoObjectId && victim_event && fields_ok);
    CHECK(died_events > 1 && died_events == deaths);
}

// #359: the trooper's Damage_Normal clone. `primary`: HIDE_WHEN_FOGGED in the clone's generic set keeps the primary path
// (a clone object); without it the infantry corpse path makes none (death_clone_corpse_path).
bool g_clone_primary = true;
std::string clone_patch(std::string t) {
    const std::string at = "<GroundInfantry Name=\"S10_Trooper\">";
    if (const size_t p = t.find(at); p != std::string::npos) t.insert(p + at.size(), "<Death_Clone>Damage_Normal, S10_Trooper_Clone</Death_Clone>");
    const size_t end = t.find("</S10>");
    if (end != std::string::npos)
        t.insert(end, std::string(" <GroundInfantry Name=\"S10_Trooper_Clone\"><Death_Persistence_Duration>5</Death_Persistence_Duration>") +
                          (g_clone_primary ? "<Behavior>HIDE_WHEN_FOGGED</Behavior>" : "") + "</GroundInfantry>\n");
    return t;
}

// #359 (gap 14; legacy S9_DEATH_CLONE_SHIELDS §1.6 [H], 0x29f270 -> 0x395590 -> 0x3a92f0): a trooper dies (script kill, processed at
// once) while its row runs Move variant 1 looped at half speed from frame 7; its clone's row starts on that slot - no idle clip, no
// synced start-frame draw. The clone model has no Die clip, so its death plays nothing over the copied slot.
void test_clone_copies_slot() {
#ifdef ALAMO_SIM_HAVE_ANIM
    g_temp_prefix = "alamo_sim_r3h_tests_359_slot";
    g_clone_primary = true;
    g_types_patch = clone_patch;
    auto d = make_data();
    g_types_patch = nullptr;
    const objects::TypeIndex trooper = d->types.resolve_name("S10_Trooper"), clone_t = d->types.resolve_name("S10_Trooper_Clone");
    CHECK(clone_t != objects::kNone);
    const auto model = synth_model("R3H_TROOPER", {{anim::kAnimIdle, 30u}, {anim::kAnimMove, 20u}, {anim::kAnimMove, 24u}, {anim::kAnimDie, 40u}});
    const auto clone_model = synth_model("R3H_CLONE", {{anim::kAnimIdle, 30u}, {anim::kAnimMove, 20u}, {anim::kAnimMove, 24u}});
    SimAnimation animation(d->types, *d->files);
    animation.set_model(trooper, objects::ModeKind::Land, model.get());
    animation.set_model(clone_t, objects::ModeKind::Land, clone_model.get());
    objects::ObjectId victim = objects::kNoObjectId, clone = objects::kNoObjectId;
    anim::SlotView at_death, first;
    land_battle(*d, &animation, 900, [&](TacticalBattle& b, TacticalMode& tm, SimHost& host) {
        objects::ObjectStore& st = tm.primary();
        anim::AnimWorld* w = animation.find_world(tm) ? &animation.world(tm) : nullptr;
        if (victim == objects::kNoObjectId) {
            if (b.frame() < 30 || !w) return true;
            for (const objects::ObjectHandle h : st.all_objects())
                if (victim == objects::kNoObjectId && st.alive(h) && st.type(h) == trooper && w->contains(st.id(h))) victim = st.id(h);
            if (victim == objects::kNoObjectId) return false;
            anim::PlayRequest req;
            req.type = anim::kAnimMove;
            req.index = 1;
            req.loop = true;
            req.speed = 0.5f;
            req.start_frame = 7.0f;
            CHECK(w->play(victim, req, nullptr, b.frame()) == anim::PlayResult::Played);
            at_death = w->slot(victim);
            CHECK(b.script_damage(tm, victim, 1.0e6f, false));
            // the pending clone's slot is saved state (anim chunk version 4): a write / read round trip between ticks keeps it
            ByteWriter bw;
            animation.write(host, bw);
            ByteReader br(bw.bytes());
            CHECK(animation.read(host, br));
            return true;
        }
        for (const objects::ObjectHandle h : st.all_objects())
            if (st.alive(h) && st.type(h) == clone_t) clone = st.id(h);
        if (clone == objects::kNoObjectId || !w || !w->contains(clone)) return b.frame() < 200;
        first = w->slot(clone);
        return false;
    });
    std::printf("  #359 clone slot: dying Move %d v%u frame %.2f speed %.2f; clone type %d v%u frame %.2f speed %.2f mode %d\n", at_death.type,
                at_death.variant, static_cast<double>(at_death.frame), static_cast<double>(at_death.speed), first.type, first.variant,
                static_cast<double>(first.frame), static_cast<double>(first.speed), static_cast<int>(first.mode));
    CHECK(clone != objects::kNoObjectId);
    CHECK(first.type == anim::kAnimMove && first.variant == 1 && first.mode == anim::SlotMode::Loop && first.speed == 0.5f);
    CHECK(first.frame >= 7.0f && first.frame <= 8.0f);  // the copied frame, advanced at most two half-speed frames
#else
    std::printf("  #359 clone slot: skipped (no engine/anim)\n");
#endif
}

// #359 (gap 20, [port] as "with a Death_Clone the original plays no death clip"): a trooper whose clone takes the infantry corpse path
// and has no animated model leaves nothing behind - no death clip, no corpse of its own type.
void test_corpse_path_no_model() {
#ifdef ALAMO_SIM_HAVE_ANIM
    g_temp_prefix = "alamo_sim_r3h_tests_359_corpse";
    g_clone_primary = false;
    g_types_patch = clone_patch;
    auto d = make_data();
    g_types_patch = nullptr;
    g_clone_primary = true;
    const objects::TypeIndex trooper = d->types.resolve_name("S10_Trooper");
    const auto model = synth_model("R3H_TROOPER", {{anim::kAnimIdle, 30u}, {anim::kAnimMove, 20u}, {anim::kAnimDie, 40u}});
    SimAnimation animation(d->types, *d->files);
    animation.set_model(trooper, objects::ModeKind::Land, model.get());
    objects::ObjectId victim = objects::kNoObjectId;
    bool corpse = false, row = false, died_on_row = false;
    uint32_t killed = 0;
    land_battle(*d, &animation, 900, [&](TacticalBattle& b, TacticalMode& tm, SimHost&) {
        objects::ObjectStore& st = tm.primary();
        const anim::AnimWorld* w = animation.find_world(tm);
        const anim::AnimBehaviors* beh = animation.find_behaviors(tm);
        if (victim == objects::kNoObjectId) {
            if (b.frame() < 30 || !w) return true;
            for (const objects::ObjectHandle h : st.all_objects())
                if (victim == objects::kNoObjectId && st.alive(h) && st.type(h) == trooper && w->contains(st.id(h))) victim = st.id(h);
            if (victim == objects::kNoObjectId) return false;
            CHECK(b.script_damage(tm, victim, 1.0e6f, false));
            killed = b.frame();
        }
        corpse = corpse || (beh && beh->corpse(victim) != nullptr);
        row = row || (w && w->contains(victim));
        died_on_row = died_on_row || (w && w->contains(victim) && w->current_type(victim) == anim::kAnimDie);
        return b.frame() < killed + 60;
    });
    std::printf("  #359 corpse path without a clone model: corpse %d, row %d, death clip %d\n", corpse ? 1 : 0, row ? 1 : 0, died_on_row ? 1 : 0);
    CHECK(victim != objects::kNoObjectId && !corpse && !row && !died_on_row);
#else
    std::printf("  #359 corpse path: skipped (no engine/anim)\n");
#endif
}

// #358 (0x58cf90 phase 0 [H]: synced 0x1ffb40(0, n - 1), no draw for n = 1; spawned at 0x398440 = position + (0, 0,
// Ranged_Target_Z_Adjust)): a trooper with Death_Explosions `list` and Ranged_Target_Z_Adjust 12 killed by script.
struct DeathBoomRun {
    uint32_t booms = 0;          // S10_Death_Boom_* objects right after the kill
    float boom_z = 0.0f, victim_z = 0.0f;
    uint64_t draws = 0;          // synced draws of the kill
};
DeathBoomRun run_death_boom(const char* list) {
    g_temp_prefix = "alamo_sim_r3h_tests_358";
    static std::string s_list;  // g_types_patch is a plain function pointer
    s_list = list;
    g_types_patch = [](std::string t) {
        const std::string at = "<GroundInfantry Name=\"S10_Trooper\">";
        if (const size_t p = t.find(at); p != std::string::npos)
            t.insert(p + at.size(), "<Death_Explosions>" + s_list + "</Death_Explosions><Ranged_Target_Z_Adjust>12</Ranged_Target_Z_Adjust>");
        if (const size_t end = t.find("</S10>"); end != std::string::npos)
            t.insert(end, " <Particle Name=\"S10_Death_Boom_A\"><Particle_Lifetime_Frames>20</Particle_Lifetime_Frames></Particle>\n"
                          " <Particle Name=\"S10_Death_Boom_B\"><Particle_Lifetime_Frames>20</Particle_Lifetime_Frames></Particle>\n");
        return t;
    };
    auto d = make_data();
    g_types_patch = nullptr;
    const objects::TypeIndex trooper = d->types.resolve_name("S10_Trooper");
    const objects::TypeIndex boom_a = d->types.resolve_name("S10_Death_Boom_A"), boom_b = d->types.resolve_name("S10_Death_Boom_B");
    CHECK(boom_a != objects::kNone && boom_b != objects::kNone);
    DeathBoomRun out;
    objects::ObjectId victim = objects::kNoObjectId;
    land_battle(*d, nullptr, 900, [&](TacticalBattle& b, TacticalMode& tm, SimHost& host) {
        objects::ObjectStore& st = tm.primary();
        if (b.frame() < 60) return true;
        for (const objects::ObjectHandle h : st.all_objects())
            if (victim == objects::kNoObjectId && st.alive(h) && st.type(h) == trooper && b.combat().alive(st.id(h))) victim = st.id(h);
        if (victim == objects::kNoObjectId) return false;
        out.victim_z = st.position(st.find(victim)).z;
        const uint64_t before = host.random().synced().draws();
        CHECK(b.script_damage(tm, victim, 1.0e6f, false));
        out.draws = host.random().synced().draws() - before;
        for (const objects::ObjectHandle h : st.all_objects())
            if (st.alive(h) && (st.type(h) == boom_a || st.type(h) == boom_b)) {
                ++out.booms;
                out.boom_z = st.position(h).z;
            }
        return false;
    });
    std::printf("  #358 Death_Explosions \"%s\": %u explosion(s) at z %.2f (victim z %.2f), kill draws %llu\n", list, out.booms,
                static_cast<double>(out.boom_z), static_cast<double>(out.victim_z), static_cast<unsigned long long>(out.draws));
    return out;
}

void test_death_explosion_pick() {
    const DeathBoomRun one = run_death_boom("S10_Death_Boom_A");
    const DeathBoomRun two = run_death_boom("S10_Death_Boom_A, S10_Death_Boom_B");
    CHECK(one.booms == 1 && two.booms == 1);
    CHECK(std::fabs(one.boom_z - (one.victim_z + 12.0f)) < 1e-3f && std::fabs(two.boom_z - (two.victim_z + 12.0f)) < 1e-3f);
    CHECK(two.draws == one.draws + 1);  // the pick: one synced Get_Int(0, 1); none for one entry
}

// #357 (DeathBehavior service 0x58d430 spin branch, path 0x58c7a0, SplineClass [H]; docs/re/SPIN_AWAY_DEATH.md): a walking
// trooper dies into S10_Spin_Wreck, which always spins away (chance 1, 1 s). The death spawns the clone's
// Spin_Away_On_Death_Explosion at the death position + (0, 0, Ranged_Target_Z_Adjust); the wreck's first service builds the
// 4-point path from V = 30 x the trooper's last-tick displacement (one synced theta); each later service moves it to the
// spline sample at dist += speed / 30, rolls +20 degrees and keeps its land obstacle with it; the service that reaches
// speed x Spin_Away_On_Death_Time spawns Death_Explosions at the last position + the lift and removes the clone through #221
// (its Death_Explosions_End). `save_load`: the battle state is written and read back mid-spin.
std::string spin_patch(std::string t) {
    const std::string at = "<GroundInfantry Name=\"S10_Trooper\">";
    if (const size_t p = t.find(at); p != std::string::npos) t.insert(p + at.size(), "<Death_Clone>Damage_Normal, S10_Spin_Wreck</Death_Clone>");
    if (const size_t end = t.find("</S10>"); end != std::string::npos)
        t.insert(end, " <GroundVehicle Name=\"S10_Spin_Wreck\"><LandBehavior>HIDE_WHEN_FOGGED</LandBehavior>"
                      "<UnitCollisionClass>Vehicle</UnitCollisionClass><IsDeathCloneObstacle>Yes</IsDeathCloneObstacle>"
                      "<Spin_Away_On_Death>Yes</Spin_Away_On_Death><Spin_Away_On_Death_Chance>1.0</Spin_Away_On_Death_Chance>"
                      "<Spin_Away_On_Death_Time>1.0</Spin_Away_On_Death_Time><Spin_Away_On_Death_Explosion>S10_Spin_Start</Spin_Away_On_Death_Explosion>"
                      "<Death_Explosions>S10_Spin_End</Death_Explosions><Death_Explosions_End>S10_Spin_Gone</Death_Explosions_End>"
                      "<Ranged_Target_Z_Adjust>12</Ranged_Target_Z_Adjust><Max_Rate_Of_Turn>4</Max_Rate_Of_Turn>"
                      "<Custom_Hard_XExtent>8</Custom_Hard_XExtent><Custom_Hard_YExtent>5</Custom_Hard_YExtent></GroundVehicle>\n"
                      " <Particle Name=\"S10_Spin_Start\"><Particle_Lifetime_Frames>200</Particle_Lifetime_Frames></Particle>\n"
                      " <Particle Name=\"S10_Spin_End\"><Particle_Lifetime_Frames>200</Particle_Lifetime_Frames></Particle>\n"
                      " <Particle Name=\"S10_Spin_Gone\"><Particle_Lifetime_Frames>200</Particle_Lifetime_Frames></Particle>\n");
    return t;
}

struct SpinRun {
    objects::ObjectId clone = objects::kNoObjectId;
    Vec3 death{}, v{};
    uint32_t starts = 0, ends = 0, gones = 0;  // the three explosions, counted once each (start: right after the kill)
    Vec3 start_at{}, end_at{};
    std::vector<Vec3> pts;   // the wreck's control points after its first service
    std::vector<Vec3> path;  // the clone's position after each tick from the kill on, while it exists
    std::vector<float> roll; // its roll (radians) then
    uint32_t services = 0;   // ticks from the kill to the clone's removal (the end service K)
    bool obstacle_follows = true, obstacle_left = false, save_same = true, saved = false;
};

SpinRun run_spin_wreck(bool save_load) {
    g_temp_prefix = save_load ? "alamo_sim_r3h_tests_357s" : "alamo_sim_r3h_tests_357";
    g_types_patch = spin_patch;
    auto d = make_data();
    g_types_patch = nullptr;
    const objects::TypeIndex trooper = d->types.resolve_name("S10_Trooper"), wreck = d->types.resolve_name("S10_Spin_Wreck");
    const objects::TypeIndex t_start = d->types.resolve_name("S10_Spin_Start"), t_end = d->types.resolve_name("S10_Spin_End"),
                             t_gone = d->types.resolve_name("S10_Spin_Gone");
    CHECK(wreck != objects::kNone && t_start != objects::kNone && t_end != objects::kNone && t_gone != objects::kNone);
    SpinRun out;
    objects::ObjectId victim = objects::kNoObjectId;
    std::vector<Vec3> hist;  // the victim's position after each tick since its move order
    uint32_t kill_frame = 0;
    const auto count = [](const objects::ObjectStore& st, objects::TypeIndex t, Vec3* at) {
        uint32_t n = 0;
        for (const objects::ObjectHandle h : st.all_objects())
            if (st.alive(h) && st.type(h) == t) {
                ++n;
                if (at) *at = st.position(h);
            }
        return n;
    };
    land_battle(*d, nullptr, 1200, [&](TacticalBattle& b, TacticalMode& tm, SimHost& host) {
        objects::ObjectStore& st = tm.primary();
        if (victim == objects::kNoObjectId) {
            if (b.frame() < 30) return true;
            for (const objects::ObjectHandle h : st.owned_by(1))
                if (victim == objects::kNoObjectId && st.alive(h) && st.type(h) == trooper && b.movement().has_unit(st.id(h))) victim = st.id(h);
            if (victim == objects::kNoObjectId) return false;
            const Vec3 p = st.position(st.find(victim));
            const objects::ObjectId ids[1] = {victim};
            CHECK(b.order_move(tm, 1, ids, p.x + 400.0f, p.y + 150.0f, host.random().synced()) == commands::RejectReason::None);
            return true;
        }
        if (out.clone == objects::kNoObjectId) {
            const objects::ObjectHandle vh = st.find(victim);
            if (!st.alive(vh)) return false;
            hist.push_back(st.position(vh));
            const size_t n = hist.size();
            if (n < 40 || !(hist[n - 2].x != hist[n - 3].x || hist[n - 2].y != hist[n - 3].y)) return n < 400;
            // between ticks: the tick just run started at hist[n - 2] (V = 30 x the displacement of the tick before it)
            out.v = Vec3{30.0f * (hist[n - 2].x - hist[n - 3].x), 30.0f * (hist[n - 2].y - hist[n - 3].y), 30.0f * (hist[n - 2].z - hist[n - 3].z)};
            out.death = hist[n - 1];
            kill_frame = b.frame();
            CHECK(b.script_damage(tm, victim, 1.0e6f, false));
            for (const objects::ObjectHandle h : st.all_objects())
                if (st.alive(h) && st.type(h) == wreck) out.clone = st.id(h);
            out.starts = count(st, t_start, &out.start_at);
            return out.clone != objects::kNoObjectId;
        }
        const objects::ObjectHandle ch = st.find(out.clone);
        if (!st.alive(ch)) {
            out.services = b.frame() - kill_frame;
            out.ends = count(st, t_end, &out.end_at);
            out.gones = count(st, t_gone, nullptr);
            out.obstacle_left = !b.movement().has_unit(out.clone);
            return false;
        }
        if (out.pts.empty()) {
            float dist = 0.0f;
            CHECK(TacticalBattleTestAccess::spin_wreck(b, out.clone, out.pts, dist));
        }
        const Vec3 p = st.position(ch);
        out.path.push_back(p);
        float r = 0.0f, pitch = 0.0f;
        CHECK(b.unit_attitude(out.clone, r, pitch));
        out.roll.push_back(r);
        const tactical::movement::UnitView mv = b.movement().view(out.clone);
        out.obstacle_follows = out.obstacle_follows && b.movement().has_unit(out.clone) && mv.pos.x == p.x && mv.pos.y == p.y;
        if (save_load && out.path.size() == 5) {  // mid-spin: the battle state written and read back in place
            StateHasher h0;
            b.hash(h0);
            ByteWriter w;
            b.write_part(TacticalBattle::SavePart::State, w);
            ByteReader rd(w.bytes());
            CHECK(b.read_part(TacticalBattle::SavePart::State, rd));
            StateHasher h1;
            b.hash(h1);
            out.save_same = h0.digest() == h1.digest();
            out.saved = true;
        }
        return b.frame() < kill_frame + 200;
    });
    std::printf("  #357 spin wreck%s: V (%.2f, %.2f, %.2f), %zu moves, end service %u, start / end / gone explosions %u / %u / %u\n",
                save_load ? " (save / load mid-spin)" : "", static_cast<double>(out.v.x), static_cast<double>(out.v.y), static_cast<double>(out.v.z),
                out.path.size(), out.services, out.starts, out.ends, out.gones);
    return out;
}

void test_spin_wreck() {
    const SpinRun a = run_spin_wreck(false);
    CHECK(a.clone != objects::kNoObjectId && a.pts.size() == 4);
    if (a.pts.size() != 4) return;
    // the death: one Spin_Away_On_Death_Explosion at the death position + (0, 0, 12)
    CHECK(a.starts == 1 && a.start_at.x == a.death.x && a.start_at.y == a.death.y && std::fabs(a.start_at.z - (a.death.z + 12.0f)) < 1e-4f);
    // the path (0x58c7a0): P0 the death position, P3 = P0 + T V, P1 their midpoint, P2 at 0.3 |P3 - P0| from P3, not below it
    const float T = 1.0f;
    const Vec3 P0 = a.death, E{P0.x + T * a.v.x, P0.y + T * a.v.y, P0.z + T * a.v.z};
    CHECK(a.pts[0].x == P0.x && a.pts[0].y == P0.y && a.pts[0].z == P0.z);
    CHECK(a.pts[3].x == E.x && a.pts[3].y == E.y && a.pts[3].z == E.z);
    CHECK(a.pts[1].x == (P0.x + E.x) * 0.5f && a.pts[1].y == (P0.y + E.y) * 0.5f && a.pts[1].z == (P0.z + E.z) * 0.5f);
    const float L = std::sqrt((E.x - P0.x) * (E.x - P0.x) + (E.y - P0.y) * (E.y - P0.y) + (E.z - P0.z) * (E.z - P0.z));
    const Vec3 q{a.pts[2].x - E.x, a.pts[2].y - E.y, a.pts[2].z - E.z};
    CHECK(L > 1.0f && std::fabs(std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z) - 0.3f * L) < 1e-3f * L && q.z >= 0.0f);
    // the services: dist += speed / 30 (float), the end at the first dist >= speed x T, each move to sample(dist)
    tactical::combat::RocketSpline sp;
    for (const Vec3& p : a.pts) sp.append(tactical::Vec3{p.x, p.y, p.z});
    sp.build(20);
    const float speed = std::sqrt((a.v.x * a.v.x + a.v.y * a.v.y) + a.v.z * a.v.z), step = speed / 30.0f, limit = speed * T;
    std::vector<Vec3> expect;
    float dist = 0.0f;
    for (;;) {
        dist = step + dist;
        tactical::Vec3 s;
        if (dist >= limit || !sp.sample(dist, s)) break;
        expect.push_back(Vec3{s.x, s.y, s.z});
    }
    std::printf("  #357 expected: %zu moves, end service %zu (speed %.3f, L %.3f)\n", expect.size(), expect.size() + 1, static_cast<double>(speed),
                static_cast<double>(L));
    CHECK(expect.size() >= 25 && a.services == expect.size() + 1 && a.path.size() == expect.size());
    bool path_ok = a.path.size() == expect.size(), roll_ok = path_ok;
    for (size_t k = 0; path_ok && k < expect.size(); ++k) {
        path_ok = a.path[k].x == expect[k].x && a.path[k].y == expect[k].y && a.path[k].z == expect[k].z;
        roll_ok = roll_ok && std::fabs(a.roll[k] - 20.0f * static_cast<float>(k + 1) * 0.017453292f) < 1e-4f;  // +20 degrees a service
    }
    CHECK(path_ok && roll_ok && a.obstacle_follows && a.obstacle_left);
    // the end: Death_Explosions at the last position + (0, 0, 12), then the #221 removal's Death_Explosions_End
    const Vec3 last = expect.empty() ? P0 : expect.back();
    CHECK(a.ends == 1 && a.end_at.x == last.x && a.end_at.y == last.y && std::fabs(a.end_at.z - (last.z + 12.0f)) < 1e-4f && a.gones == 1);
    // save / load mid-spin: the same digest, then the same path to the same end
    const SpinRun b = run_spin_wreck(true);
    CHECK(b.saved && b.save_same && b.services == a.services && b.path.size() == a.path.size() && b.ends == 1 && b.gones == 1);
    bool same = b.path.size() == a.path.size();
    for (size_t k = 0; same && k < a.path.size(); ++k) same = b.path[k].x == a.path[k].x && b.path[k].y == a.path[k].y && b.path[k].z == a.path[k].z;
    CHECK(same);
}

int main() {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    std::printf("r3h t01: Force_Sight clips on the battle's animation world\n");
    test_force_sight_rows();
    test_force_sight_death(true);
    test_force_sight_death(false);
    test_force_sight_no_end();
    test_pad_sell_start();  // tru3 t06
    test_unit_died_presentation();  // #327 t06
    test_clone_copies_slot();       // #359
    test_corpse_path_no_model();    // #359
    test_death_explosion_pick();    // #358
    test_spin_wreck();              // #357
    if (g_fail) {
        std::printf("alamo_sim_r3h_tests: %d failure(s)\n", g_fail);
        return 1;
    }
    std::printf("alamo_sim_r3h_tests: OK\n");
    return 0;
}
