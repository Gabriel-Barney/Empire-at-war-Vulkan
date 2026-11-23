// alamo_game_audio_host_tests: GameAudioHost's movie music (STRATEGIC_GAPS §7.3 / §7.4), data-free: a synthetic audio tree
// in memory and the null backend. The movie-music flag (Movies.xml Has_Credits) starts mode 14 music and the movie's end
// restores the music it replaced; Halt_Game_Music stops the music for the movie and brings the mode music back after it;
// a MusicEvent_Frame cue is mode 17. Movies without those flags never reach the host (MovieSlot only calls it for them).
// Without engine/audio the host is inactive: the test then only checks that every call is a harmless no-op.
#include <cstdio>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "alamo/core/file_provider.hpp"
#include "alamo/game/game_audio_host.hpp"
#include "alamo/game/game_data.hpp"
#include "alamo/tactical/combat/hero_clash.hpp"
#include "alamo/objects/object_types.hpp"            // aud1 t05
#include "alamo/tactical/abilities/ability_types.hpp"  // aud1 t05
#include "alamo/test/support.hpp"
#ifdef ALAMO_GAME_HAVE_AUDIO
#include "alamo/audio/audio_system.hpp"
#endif
#ifdef ALAMO_AUDIO_HAVE_LUA
#include "alamo/lua/presentation.hpp"  // aud8 t05
#endif

using namespace alamo;
using namespace alamo::game;

using ::alamo::test::g_fail;

namespace {

class MemoryProvider final : public FileProvider {
public:
    void add(const std::string& path, const std::string& text) {
        files_[normalize_path(path)] = std::vector<uint8_t>(text.begin(), text.end());
    }
    bool read(std::string_view path, std::vector<uint8_t>& out) const override {
        auto it = files_.find(normalize_path(path));
        if (it == files_.end()) return false;
        out = it->second;
        return true;
    }
    bool exists(std::string_view path) const override { return files_.count(normalize_path(path)) != 0; }
    std::string origin(std::string_view path) const override { return exists(path) ? "mem:" + std::string(path) : ""; }

private:
    std::map<std::string, std::vector<uint8_t>> files_;
};

const char* kSfx = R"(<?xml version="1.0"?>
<SFXEvents>
  <SFXEvent Name="GUI_Click"><Is_2D>Yes</Is_2D><Is_3D>No</Is_3D><Is_GUI>Yes</Is_GUI><Samples>CLICK.WAV</Samples></SFXEvent>
</SFXEvents>
)";
const char* kMusic = R"(<?xml version="1.0"?>
<MusicEvents>
  <MusicEvent Name="Gal_Rebel"><Files>G1.MP3</Files><Loop>Yes</Loop></MusicEvent>
  <MusicEvent Name="Game_Credits_Music_Event"><Files>C1.MP3, C2.MP3, C3.MP3</Files><Loop>Yes</Loop></MusicEvent>
  <MusicEvent Name="Main_Menu_Music_Event"><Files>MENU.MP3</Files><Loop>Yes</Loop></MusicEvent>
  <MusicEvent Name="Movie_Cue"><Files>CUE.MP3</Files></MusicEvent>
  <MusicEvent Name="Amb_Space"><Files>AS.MP3</Files><Loop>Yes</Loop></MusicEvent>
  <MusicEvent Name="Amb_Space_DS"><Files>ADS.MP3</Files><Loop>Yes</Loop></MusicEvent>
  <MusicEvent Name="Structure_Hutt_Palace_Music_Event"><Files>HP1.MP3, HP2.MP3</Files></MusicEvent>
  <MusicEvent Name="Structure_Cantina_Music_Event"><Files>CA1.MP3, CA2.MP3</Files></MusicEvent>
</MusicEvents>
)";
const char* kFactions = R"(<?xml version="1.0" ?>
<Factions>
  <Faction Name="Rebel"><Strategic_Map_Music_Event> Gal_Rebel </Strategic_Map_Music_Event>
    <Music_Event_List_Ambient> Space, Amb_Space </Music_Event_List_Ambient>
    <Music_Event_Space_Ambient_Super_Weapon> Amb_Space_DS </Music_Event_Space_Ambient_Super_Weapon></Faction>
</Factions>
)";

[[maybe_unused]] std::unique_ptr<MemoryProvider> make_tree(bool credits_music) {
    auto p = std::make_unique<MemoryProvider>();
    p->add("Data\\XML\\SFXEventFiles.xml", "<SFXEvent_Files><File>SFXEventsTest.xml</File></SFXEvent_Files>");
    p->add("Data\\XML\\SFXEventsTest.xml", kSfx);
    std::string music = kMusic;
    if (!credits_music) {  // the event the credits movie asks for is missing from the database
        const size_t a = music.find("  <MusicEvent Name=\"Game_Credits_Music_Event\"");
        const size_t b = music.find('\n', a);
        music.erase(a, b - a + 1);
    }
    p->add("Data\\XML\\MusicEvents.xml", music);
    p->add("Data\\XML\\SpeechEvents.xml", "<SpeechEvents/>");
    p->add("Data\\XML\\Audio.xml", "<Audio/>");
    p->add("Data\\XML\\FactionFiles.xml", "<Faction_Files><File>Factions.xml</File></Faction_Files>");
    p->add("Data\\XML\\Factions.xml", kFactions);
    return p;
}

#ifdef ALAMO_GAME_HAVE_AUDIO
std::string cur(GameAudioHost& h) { return h.system()->current_music(); }
uint8_t mode(GameAudioHost& h) { return h.system()->music_mode(); }

// #62 t07 (0x44890 [H]): Alt + Shift and a selected Hutt palace / cantina start its music, forced, in the current mode
// (mode 6 when there is none).
void test_structure_music() {
    GameData data;
    data.files = make_tree(true);
    GameAudioHost h;
    std::string err;
    CHECK(h.init(data, AudioBackendKind::Null, 7, &err));
    if (!h.active()) return;
    CHECK(mode(h) == audio::music_mode::kNone);
    CHECK(!h.structure_music("Ground_Hutt_Palace_E", true, false) && !h.structure_music("Ground_Hutt_Palace_E", false, true));
    CHECK(cur(h).empty());
    CHECK(h.structure_music("Ground_Hutt_Palace_E", true, true));
    CHECK(cur(h) == "Structure_Hutt_Palace_Music_Event" && mode(h) == audio::music_mode::kTacticalAmbient);
    h.music(MusicScene::Galactic, "Rebel");
    CHECK(cur(h) == "Gal_Rebel" && mode(h) == audio::music_mode::kGalactic);
    CHECK(h.structure_music("ground_cantina_r", true, true));  // _stricmp
    CHECK(cur(h) == "Structure_Cantina_Music_Event" && mode(h) == audio::music_mode::kGalactic);
    CHECK(h.structure_music("Ground_Hutt_Palace_R", true, true) && cur(h) == "Structure_Hutt_Palace_Music_Event");
    CHECK(h.structure_music("Ground_Hutt_Palace_E", true, true));  // forced: the playing event restarts
    CHECK(h.structure_music("Ground_Cantina_E", true, true) && cur(h) == "Structure_Cantina_Music_Event");
    CHECK(!h.structure_music("Ground_Cantina_X", true, true) && !h.structure_music("Ground_Hutt_Palace", true, true));
    CHECK(cur(h) == "Structure_Cantina_Music_Event");
}

void test_movie_music() {
    GameData data;
    data.files = make_tree(true);
    GameAudioHost h;
    std::string err;
    CHECK(h.init(data, AudioBackendKind::Null, 7, &err));
    CHECK(h.active() && h.system());
    if (!h.active()) return;
    h.music(MusicScene::Galactic, "Rebel");
    CHECK(cur(h) == "Gal_Rebel" && mode(h) == audio::music_mode::kGalactic);

    // The movie-music flag (Has_Credits): mode 14, the credits event; the movie ending restores the galactic music.
    h.movie_started(true, false);
    CHECK(cur(h) == "Game_Credits_Music_Event" && mode(h) == audio::music_mode::kMovie && mode(h) == 14);
    CHECK(h.stats().movie_music == 1);
    h.movie_ended();
    CHECK(cur(h) == "Gal_Rebel" && mode(h) == audio::music_mode::kGalactic);
    CHECK(h.stats().movie_restores == 1);
    h.movie_ended();  // nothing to undo: no change
    CHECK(cur(h) == "Gal_Rebel" && h.stats().movie_restores == 1);

    // Halt_Game_Music: silence while the movie plays, the mode music comes back with the movie's end.
    h.movie_started(false, true);
    CHECK(cur(h).empty() && h.stats().movie_halts == 1);
    h.movie_ended();
    CHECK(cur(h) == "Gal_Rebel" && mode(h) == audio::music_mode::kGalactic);

    // Halt wins over the movie-music flag (a halted movie plays no music of its own).
    h.movie_started(true, true);
    CHECK(cur(h).empty() && h.stats().movie_music == 1);
    h.movie_ended();
    CHECK(cur(h) == "Gal_Rebel");

    // A music cue (17) replaces the music; the movie's end restores it.
    h.movie_cue("Movie_Cue");
    CHECK(cur(h) == "Movie_Cue" && mode(h) == audio::music_mode::kMovieCue && mode(h) == 17);
    CHECK(h.stats().movie_cues == 1);
    h.movie_ended();
    CHECK(cur(h) == "Gal_Rebel" && mode(h) == audio::music_mode::kGalactic);
    h.movie_cue("");  // empty names and unknown events change nothing
    h.movie_cue("No_Such_Event");
    CHECK(cur(h) == "Gal_Rebel");
    h.movie_ended();
    CHECK(cur(h) == "Gal_Rebel");

    // The movie's music already gave way to something else (the new mode's music): ending the movie leaves it alone.
    h.movie_started(true, false);
    CHECK(mode(h) == audio::music_mode::kMovie);
    h.music(MusicScene::MainMenu, "Rebel");  // a mode change replaces the movie music
    h.movie_ended();
    CHECK(mode(h) != audio::music_mode::kMovie);
}

void test_missing_credits_event() {
    GameData data;
    data.files = make_tree(false);
    GameAudioHost h;
    CHECK(h.init(data, AudioBackendKind::Null, 7));
    if (!h.active()) return;
    h.music(MusicScene::Galactic, "Rebel");
    h.movie_started(true, false);  // no such event: the music stays, nothing to restore
    CHECK(cur(h) == "Gal_Rebel" && h.stats().movie_music == 0);
    h.movie_ended();
    CHECK(cur(h) == "Gal_Rebel" && h.stats().movie_restores == 0);
}

// trl2 t08 (legacy docs/re/AUDIO.md §9.5, MCX4_T10_RE_ANSWERS §3): a shot held by Projectile_Appearance_Delay_Frames plays no fire sound at
// the shot; its sound is handed out on the frame it appears (or at the first battle tick after it), in fire order.
void test_delayed_fire_sounds() {
    DelayedFireSounds q;
    CHECK(!q.offer({1, 100, 7, -1, "", ""}, 100));  // appears this frame: plays now
    CHECK(q.offer({2, 104, 7, 3, "", ""}, 100));    // 4-frame delay
    CHECK(q.offer({3, 102, 8, -1, "", ""}, 100));
    std::vector<uint32_t> got;
    auto take = [&](uint32_t now) { q.take_due(now, [&](const DelayedFireSounds::Shot& s) { got.push_back(s.serial); }); };
    take(101);
    CHECK(got.empty() && q.size() == 2);
    take(102);
    CHECK(got.size() == 1 && got[0] == 3 && q.size() == 1);
    take(110);  // a late tick still hands it out once
    CHECK(got.size() == 2 && got[1] == 2 && q.size() == 0);
    take(111);
    CHECK(got.size() == 2);
    q.offer({4, 200, 9, -1, "", ""}, 150);
    q.clear();  // battle_reset
    CHECK(q.size() == 0);
}

// aud4 b0286 (legacy docs/re/AUDIO.md §2.3): the first super-weapon report of a battle re-picks the ambient music with the
// _Super_Weapon variant; later reports only set the flag; battle_reset starts a new battle's first report.
void test_super_weapon_music() {
    GameData data;
    data.files = make_tree(true);
    GameAudioHost h;
    CHECK(h.init(data, AudioBackendKind::Null, 7));
    if (!h.active()) return;
    h.music(MusicScene::Battle, "Rebel", true);
    CHECK(cur(h) == "Amb_Space" && mode(h) == audio::music_mode::kTacticalAmbient);
    h.super_weapon_report(true);
    CHECK(cur(h) == "Amb_Space_DS" && mode(h) == audio::music_mode::kTacticalAmbient);
    h.super_weapon_report(false);  // the super weapon died: the music stays until the next mode switch
    CHECK(cur(h) == "Amb_Space_DS");
    h.battle_reset();
    h.super_weapon_report(true);  // a new battle's first report: the same event is already playing (not restarted)
    CHECK(cur(h) == "Amb_Space_DS");
    h.battle_reset();
    h.super_weapon_report(false);
    CHECK(cur(h) == "Amb_Space");
}
#endif

// mcx4 t13 batch A (legacy docs/re/MCX4_T13_RE_ANSWERS.md, both builds): the health warning and hard point lost line rules.
// "GameObjectType.SFXEvent_Health_Low_Warning family" (Take_Damage 0x3a9e30) and "GameObjectType.SFXEvent_Hardpoint_Destroyed
// family" (0x386660).
// ledger: verifies GameObjectType.SFXEvent_Health_Low_Warning
// ledger: verifies GameObjectType.SFXEvent_Health_Critical_Warning
// ledger: verifies GameObjectType.SFXEvent_Enemy_Damaged_Health_Low_Warning
// ledger: verifies GameObjectType.SFXEvent_Enemy_Damaged_Health_Critical_Warning
// ledger: verifies GameObjectType.SFXEvent_Hardpoint_Destroyed
// ledger: verifies GameObjectType.SFXEvent_Hardpoint_All_Weapons_Destroyed
// ledger: verifies GameObjectType.SFXEvent_Attack_Hardpoint
void test_health_and_hardpoint_lines() {
    using H = GameAudioHost;
    // Thresholds 0.33 / 0.10 (FoC); critical tested first; one crossing per hit.
    CHECK(H::health_warning_tag(0.5f, 0.3f, 0.33f, 0.10f, true, false) == "SFXEvent_Health_Low_Warning");
    CHECK(H::health_warning_tag(0.5f, 0.05f, 0.33f, 0.10f, true, false) == "SFXEvent_Health_Critical_Warning");  // skips low
    CHECK(H::health_warning_tag(0.3f, 0.2f, 0.33f, 0.10f, true, false).empty());   // already below low
    CHECK(H::health_warning_tag(0.5f, 0.3f, 0.33f, 0.10f, false, true) == "SFXEvent_Enemy_Damaged_Health_Low_Warning");
    CHECK(H::health_warning_tag(0.2f, 0.1f, 0.33f, 0.10f, false, true) == "SFXEvent_Enemy_Damaged_Health_Critical_Warning");
    CHECK(H::health_warning_tag(0.5f, 0.3f, 0.33f, 0.10f, false, false).empty());  // neither side local
    CHECK(H::health_warning_tag(0.5f, 0.3f, 0.33f, 0.10f, true, true) == "SFXEvent_Health_Low_Warning");  // victim first
    // Hard point lost: local live parent only; a weapon kind with no weapon left -> all weapons; else last of the kind.
    CHECK(H::hardpoint_lost_tag(5, true, false, true, false) == "SFXEvent_Hardpoint_All_Weapons_Destroyed");
    CHECK(H::hardpoint_lost_tag(5, true, false, true, true) == "SFXEvent_Hardpoint_Destroyed");
    CHECK(H::hardpoint_lost_tag(5, true, false, false, true).empty());  // one of that kind still up
    CHECK(H::hardpoint_lost_tag(1, true, false, true, false) == "SFXEvent_Hardpoint_Destroyed");  // shield generator
    CHECK(H::hardpoint_lost_tag(5, false, false, true, false).empty() && H::hardpoint_lost_tag(5, true, true, true, false).empty());
    // The per-kind list: "HARD_POINT_WEAPON_LASER, Ev" lines; an empty event keeps the list aligned; a later line wins.
    const std::vector<std::string_view> toks = {"HARD_POINT_WEAPON_LASER", "Lost_Laser", "HARD_POINT_WEAPON_MISSILE",
                                                "HARD_POINT_ENGINE", "Lost_Engine", "HARD_POINT_WEAPON_LASER", "Lost_Laser_2"};
    CHECK(H::hardpoint_kind_event(toks, 5) == "Lost_Laser_2" && H::hardpoint_kind_event(toks, 0) == "Lost_Engine");
    CHECK(H::hardpoint_kind_event(toks, 6).empty() && H::hardpoint_kind_event(toks, 1).empty() && H::hardpoint_kind_event(toks, 13).empty());
    // The same list shape is the attack response by hard point kind ("GameObjectType.SFXEvent_Attack_Hardpoint": FoC
    // "HARD_POINT_WEAPON_LASER, Unit_HP_LASER_Corvette" lines; unit_response takes the attack's kind).
    const std::vector<std::string_view> atk = {"HARD_POINT_WEAPON_LASER", "Unit_HP_LASER_Corvette", "HARD_POINT_WEAPON_ION_CANNON",
                                               "Unit_HP_ION_Corvette"};
    CHECK(H::hardpoint_kind_event(atk, 5) == "Unit_HP_LASER_Corvette" && H::hardpoint_kind_event(atk, 0).empty());
}

// mcx4 t13 batch C (legacy docs/re/MCX4_T13_RE_ANSWERS.md "Faction.SFXEvent_Tactical_Gain_Friendly_Control family", capture
// complete 0x500e30; both builds): which halves a capture point owner change plays for the local player.
// ledger: verifies Faction.SFXEvent_Tactical_Gain_Friendly_Control
// ledger: verifies Faction.SFXEvent_Tactical_Lose_Friendly_Control
// ledger: verifies Faction.SFXEvent_Tactical_Gain_Enemy_Control
// ledger: verifies Faction.SFXEvent_Tactical_Lose_Enemy_Control
void test_control_lines() {
    using H = GameAudioHost;
    // Args: friendly_old, friendly_new, hostile_old, hostile_new, old_neutral, new_neutral.
    // Neutral -> local: gain friendly, no enemy half (old owner neutral; new owner not hostile).
    H::ControlLines c = H::control_lines(false, true, false, false, true, false);
    CHECK(c.friendly == "SFXEvent_Tactical_Gain_Friendly_Control" && c.enemy.empty());
    // Enemy -> local: gain friendly and lose enemy.
    c = H::control_lines(false, true, true, false, false, false);
    CHECK(c.friendly == "SFXEvent_Tactical_Gain_Friendly_Control" && c.enemy == "SFXEvent_Tactical_Lose_Enemy_Control");
    // Local -> enemy: lose friendly (friendly to the old owner wins) and gain enemy.
    c = H::control_lines(true, false, false, true, false, false);
    CHECK(c.friendly == "SFXEvent_Tactical_Lose_Friendly_Control" && c.enemy == "SFXEvent_Tactical_Gain_Enemy_Control");
    // Enemy -> neutral (no owner): no friendly half, lose enemy.
    c = H::control_lines(false, false, true, false, false, true);
    CHECK(c.friendly.empty() && c.enemy == "SFXEvent_Tactical_Lose_Enemy_Control");
    // Neutral -> enemy: gain enemy only; a neutral new owner gives no gain-enemy line.
    c = H::control_lines(false, false, false, true, true, false);
    CHECK(c.friendly.empty() && c.enemy == "SFXEvent_Tactical_Gain_Enemy_Control");
    c = H::control_lines(false, false, false, true, true, true);
    CHECK(c.friendly.empty() && c.enemy.empty());
}

// mcx4 t13 batch D (legacy docs/re/MCX4_T13_RE_ANSWERS.md "Faction.SFXEvent_HUD_Won_Land_Battle family", 0x343810 from the
// victory declare 0x343b90; both builds): the local faction's HUD line for a declared victory.
// ledger: verifies Faction.SFXEvent_HUD_Won_Land_Battle
// ledger: verifies Faction.SFXEvent_HUD_Lost_Land_Battle
// ledger: verifies Faction.SFXEvent_HUD_Won_Space_Battle
// ledger: verifies Faction.SFXEvent_HUD_Lost_Space_Battle
// ledger: verifies Faction.SFXEvent_HUD_Won_Tactical_Corruption_Mission
// ledger: verifies Faction.SFXEvent_HUD_Lost_Tactical_Corruption_Mission
// ledger: verifies Faction.SFXEvent_HUD_Won_Land_Battle_Enemy_TSW_Present
// ledger: verifies Faction.SFXEvent_HUD_Lost_Land_Battle_Enemy_TSW_Present
// ledger: verifies Faction.SFXEvent_HUD_Won_Space_Battle_Enemy_TSW_Present
// ledger: verifies Faction.SFXEvent_HUD_Lost_Space_Battle_Enemy_TSW_Present
// ledger: verifies Faction.Space_Forced_Retreat_Due_To_Superweapon
void test_battle_result_lines() {
    using H = GameAudioHost;
    // mcx4 t13 (legacy docs/re/MCX4_T13_RE_ANSWERS.md "Faction.Music_Event_Tactical_Win family: Ghidra follow-up"): the declare's
    // music keys. Args: local, winner, attacker, defender.
    H::ResultMusic m = H::result_music(0, 0, 0, 1);  // local attacker wins: win music keyed by the defender
    CHECK(m.play && m.won && m.other == 1);
    m = H::result_music(0, 1, 0, 1);  // local attacker loses: lose music keyed by the winner
    CHECK(m.play && !m.won && m.other == 1);
    m = H::result_music(2, 1, 0, 1);  // an ally / third side: lose music keyed by the winner
    CHECK(m.play && !m.won && m.other == 1);
    CHECK(!H::result_music(0, 0, 0, -1).play && !H::result_music(-1, 0, 0, 1).play);  // no loser / no local player
    // Args: local_won, local_hostile, space, enemy_tsw, own_killer, corruption.
    CHECK(H::battle_result_line(true, false, false, false, false, false) == "SFXEvent_HUD_Won_Land_Battle");
    CHECK(H::battle_result_line(true, false, false, true, false, true) == "SFXEvent_HUD_Won_Land_Battle_Enemy_TSW_Present");
    CHECK(H::battle_result_line(true, false, false, false, false, true) == "SFXEvent_HUD_Won_Tactical_Corruption_Mission");
    CHECK(H::battle_result_line(true, false, true, false, false, false) == "SFXEvent_HUD_Won_Space_Battle");
    CHECK(H::battle_result_line(true, false, true, true, true, false) == "SFXEvent_HUD_Won_Space_Battle_Enemy_TSW_Present");
    CHECK(H::battle_result_line(true, false, true, true, false, false) == "Space_Forced_Retreat_Due_To_Superweapon");
    CHECK(H::battle_result_line(true, false, true, false, false, true) == "SFXEvent_HUD_Won_Tactical_Corruption_Mission");
    CHECK(H::battle_result_line(false, true, false, false, false, false) == "SFXEvent_HUD_Lost_Land_Battle");
    CHECK(H::battle_result_line(false, true, true, false, false, false) == "SFXEvent_HUD_Lost_Space_Battle");
    CHECK(H::battle_result_line(false, true, false, true, false, false) == "SFXEvent_HUD_Lost_Land_Battle_Enemy_TSW_Present");
    CHECK(H::battle_result_line(false, true, true, true, true, false) == "SFXEvent_HUD_Lost_Space_Battle_Enemy_TSW_Present");
    CHECK(H::battle_result_line(false, true, true, false, false, true) == "SFXEvent_HUD_Lost_Tactical_Corruption_Mission");
    CHECK(H::battle_result_line(false, false, false, false, false, false).empty());  // an ally of the winner hears nothing
    // The declare gate: victory types 0xb..0x16, not silent.
    CHECK(H::battle_result_gate(0x0b, false) && H::battle_result_gate(0x16, false) && H::battle_result_gate(0x13, false));
    CHECK(!H::battle_result_gate(0x0a, false) && !H::battle_result_gate(0x17, false) && !H::battle_result_gate(0x10, true));
}

// mcx4 t13 batch E (legacy docs/re/MCX4_T13_RE_ANSWERS.md "Faction.Land_Retreat_Begin_SFXEvent family"; both builds).
// ledger: verifies Faction.Land_Retreat_Begin_SFXEvent
// ledger: verifies Faction.Space_Retreat_Begin_SFXEvent
// ledger: verifies Faction.Land_Retreat_Cancel_SFXEvent
// ledger: verifies Faction.Space_Retreat_Cancel_SFXEvent
// ledger: verifies Faction.Land_Retreat_Enemy_Begin_SFXEvent
// ledger: verifies Faction.Space_Retreat_Enemy_Begin_SFXEvent
void test_retreat_begin_lines() {
    using H = GameAudioHost;
    CHECK(H::retreat_begin_tag(true, false, false) == "Land_Retreat_Begin_SFXEvent");
    CHECK(H::retreat_begin_tag(true, false, true) == "Space_Retreat_Begin_SFXEvent");
    CHECK(H::retreat_begin_tag(false, true, false) == "Land_Retreat_Enemy_Begin_SFXEvent");
    CHECK(H::retreat_begin_tag(false, true, true) == "Space_Retreat_Enemy_Begin_SFXEvent");
    CHECK(H::retreat_begin_tag(false, false, true).empty());  // an ally of the retreater hears nothing
    // cancel lines (land: the local retreater's own cancel only; space: any cancel, retreater's faction).
    CHECK(H::retreat_cancel_tag(false, true, true) == "Land_Retreat_Cancel_SFXEvent");
    CHECK(H::retreat_cancel_tag(false, true, false).empty());   // land automatic cancel: silent
    CHECK(H::retreat_cancel_tag(false, false, true).empty());   // land: an enemy's cancel is not heard
    CHECK(H::retreat_cancel_tag(true, false, false) == "Space_Retreat_Cancel_SFXEvent");
    CHECK(H::retreat_cancel_tag(true, true, true) == "Space_Retreat_Cancel_SFXEvent");
    CHECK(H::retreat_cancel_type_first(true, true, false) && !H::retreat_cancel_type_first(true, false, true));
    CHECK(H::retreat_cancel_type_first(false, true, true) && !H::retreat_cancel_type_first(false, true, false));
}

// mcx4 t13 batch G (legacy docs/re/MCX4_T13_RE_ANSWERS.md "Faction.SFXEvent_HUD_Special_Weapon_Ready family"; both builds).
// ledger: verifies Faction.SFXEvent_HUD_Special_Weapon_Ready
// ledger: verifies Faction.SFXEvent_HUD_Enemy_Special_Weapon_Ready
// ledger: verifies Faction.SFXEvent_HUD_Special_Weapon_Firing
// ledger: verifies Faction.SFXEvent_HUD_Enemy_Special_Weapon_Firing
void test_special_weapon_lines() {
    using H = GameAudioHost;
    CHECK(H::special_weapon_tag(false, true) == "SFXEvent_HUD_Special_Weapon_Ready");
    CHECK(H::special_weapon_tag(false, false) == "SFXEvent_HUD_Enemy_Special_Weapon_Ready");
    CHECK(H::special_weapon_tag(true, true) == "SFXEvent_HUD_Special_Weapon_Firing");
    CHECK(H::special_weapon_tag(true, false) == "SFXEvent_HUD_Enemy_Special_Weapon_Firing");
    // FoC lists: one event per occurrence (hypervelocity gun, ion cannon, an empty third), picked by the target action index.
    const std::vector<std::pair<std::string, std::string>> f = {{"SFXEvent_HUD_Special_Weapon_Ready", "HV_Ready"},
                                                                {"SFXEvent_HUD_Enemy_Special_Weapon_Ready", "E_HV"},
                                                                {"sfxevent_hud_special_weapon_ready", "Ion_Ready"},
                                                                {"SFXEvent_HUD_Special_Weapon_Ready", ""}};
    CHECK(H::nth_field(f, "SFXEvent_HUD_Special_Weapon_Ready", 0) == "HV_Ready");
    CHECK(H::nth_field(f, "SFXEvent_HUD_Special_Weapon_Ready", 1) == "Ion_Ready");
    CHECK(H::nth_field(f, "SFXEvent_HUD_Special_Weapon_Ready", 2).empty() && H::nth_field(f, "SFXEvent_HUD_Special_Weapon_Ready", 3).empty());
    CHECK(H::nth_field(f, "SFXEvent_HUD_Enemy_Special_Weapon_Ready", 0) == "E_HV" && H::nth_field(f, "X", -1).empty());
}

// mcx4 t13 (legacy docs/re/MCX4_T13_RE_ANSWERS.md "GameObjectType.SFXEvent_Move_Into_Asteroid_Field family"; both builds).
// ledger: verifies GameObjectType.SFXEvent_Move_Into_Asteroid_Field
// ledger: verifies GameObjectType.SFXEvent_Move_Into_Nebula
void test_move_into_field() {
    using H = GameAudioHost;
    CHECK(H::move_into_field_tag(1u) == "SFXEvent_Move_Into_Asteroid_Field");
    CHECK(H::move_into_field_tag(4u) == "SFXEvent_Move_Into_Nebula");
    CHECK(H::move_into_field_tag(5u) == "SFXEvent_Move_Into_Asteroid_Field");  // asteroid tested first
    CHECK(H::move_into_field_tag(0u).empty() && H::move_into_field_tag(2u).empty() && H::move_into_field_tag(8u).empty());
}

// mcx4 t13 batch I (legacy docs/re/MCX4_T13_RE_ANSWERS.md "Faction.SFXEvent_HUD_Base_Shield_Online family"; both builds).
// ledger: verifies Faction.SFXEvent_HUD_Base_Shield_Online
// ledger: verifies Faction.SFXEvent_HUD_Base_Shield_Offline
// ledger: verifies Faction.SFXEvent_HUD_Enemy_Base_Shield_Online
// ledger: verifies Faction.SFXEvent_HUD_Enemy_Base_Shield_Offline
void test_base_shield_lines() {
    using H = GameAudioHost;
    CHECK(H::base_shield_line(true, true) == "SFXEvent_HUD_Base_Shield_Online");
    CHECK(H::base_shield_line(false, true) == "SFXEvent_HUD_Base_Shield_Offline");
    CHECK(H::base_shield_line(true, false) == "SFXEvent_HUD_Enemy_Base_Shield_Online");
    CHECK(H::base_shield_line(false, false) == "SFXEvent_HUD_Enemy_Base_Shield_Offline");
}

// mcx4 t13 batch J (legacy docs/re/P4_SIM_SYSTEMS.md §2.7.3; legacy docs/re/MCX4_T13_RE_ANSWERS.md "Faction.SFXEvent_Tactical_Unit_Cap_Reached
// family"): SFXEvent_Tactical_Pop_Cap_Reached plays when the room for one unit turns false.
// ledger: verifies Faction.SFXEvent_Tactical_Pop_Cap_Reached
void test_unit_room() {
    using H = GameAudioHost;
    CHECK(H::unit_room(3.0f, -1.0f) && H::unit_room(9.0f, 10.0f) && !H::unit_room(10.0f, 10.0f) && !H::unit_room(11.0f, 10.0f));
}

// mcx4 t13 batch M (legacy docs/re/MCX4_T13_RE_ANSWERS.md "Faction.SFXEvent_Bombard_Available family"; both builds).
// ledger: verifies Faction.SFXEvent_Bombard_Available
// ledger: verifies Faction.SFXEvent_Bombard_Ally_Available
// ledger: verifies Faction.SFXEvent_Bombard_Enemy_Available
// ledger: verifies Faction.SFXEvent_Bombing_Run_Available
// ledger: verifies Faction.SFXEvent_Bombing_Run_Ally_Available
// ledger: verifies Faction.SFXEvent_Bombing_Run_Enemy_Available
void test_strike_available_tags() {
    using H = GameAudioHost;
    CHECK(H::strike_available_tag(true, true, true) == "SFXEvent_Bombard_Available");
    CHECK(H::strike_available_tag(true, false, true) == "SFXEvent_Bombard_Ally_Available");
    CHECK(H::strike_available_tag(true, false, false) == "SFXEvent_Bombard_Enemy_Available");
    CHECK(H::strike_available_tag(false, true, false) == "SFXEvent_Bombing_Run_Available");  // local wins over relation
    CHECK(H::strike_available_tag(false, false, true) == "SFXEvent_Bombing_Run_Ally_Available");
    CHECK(H::strike_available_tag(false, false, false) == "SFXEvent_Bombing_Run_Enemy_Available");
}

// mcx4 t13 batch O (legacy docs/re/MCX4_T13_RE_ANSWERS.md "GameObjectType.Land_Damage_SFX"; both builds): the stage of a health
// fraction is the last Land_Damage_Thresholds entry it is at or below.
// ledger: verifies GameObjectType.Land_Damage_SFX
// mcx4 t13 batch P (legacy docs/re/MCX4_T13_RE_ANSWERS.md "Faction.SFXEvent_Tactical_Object_Building_Started family"; both
// builds): a local builder / seller hears the type line; every owner gets the faction line, a start also the loop.
// ledger: verifies Faction.SFXEvent_Tactical_Object_Building_Started
// ledger: verifies Faction.SFXEvent_Tactical_Object_Building_Loop
// ledger: verifies Faction.SFXEvent_Tactical_Object_Building_Complete
// ledger: verifies Faction.SFXEvent_Tactical_Object_Sold
// ledger: verifies GameObjectType.SFXEvent_Sold_Tactical
// mcx4 t13 batch S (legacy docs/re/MCX4_T13_RE_ANSWERS.md "Faction.SpeechEvent_Super_Weapon_Moving_Range_60_Seconds family"):
// the highest threshold crossed in a step, and the own / enemy speech tags.
// ledger: verifies Faction.SpeechEvent_Super_Weapon_Moving_Range_60_Seconds
// ledger: verifies Faction.SpeechEvent_Super_Weapon_Moving_Range_30_Seconds
// ledger: verifies Faction.SpeechEvent_Super_Weapon_Moving_Range_15_Seconds
// ledger: verifies Faction.SpeechEvent_Super_Weapon_Moving_Range_05_Seconds
// ledger: verifies Faction.SpeechEvent_Super_Weapon_Enemy_Moving_Range_60_Seconds
// ledger: verifies Faction.SpeechEvent_Super_Weapon_Enemy_Moving_Range_30_Seconds
// ledger: verifies Faction.SpeechEvent_Super_Weapon_Enemy_Moving_Range_15_Seconds
// ledger: verifies Faction.SpeechEvent_Super_Weapon_Enemy_Moving_Range_05_Seconds
// ledger: verifies Faction.SpeechEvent_Super_Weapon_Moved_Into_Range
// ledger: verifies Faction.SpeechEvent_Super_Weapon_Enemy_Moved_Into_Range
// mcx4 t13 batch U (legacy docs/re/MCX4_T13_RE_ANSWERS.md "Faction.SFXEvent_HUD_Reinforcement_Point_Contested"): the local
// player's reinforcement point starting to be neutralised.
// ledger: verifies Faction.SFXEvent_HUD_Reinforcement_Point_Contested
// mcx4 (legacy docs/re/MCX4_T11_T12_RE_ANSWERS.md §C3): GameConstants Unit_Command_Rankings_By_Category picks the speaking unit.
// ledger: verifies GameConstants.Unit_Command_Rankings_By_Category
// mcx4 t13 (legacy docs/re/MCX4_T13_RE_ANSWERS.md "GameObjectType.SFXEvent_Turret_Rotating_Loop"): the motor loop runs while
// the turret pose changes from tick to tick.
// ledger: verifies GameObjectType.SFXEvent_Turret_Rotating_Loop
// mcx4 t13 (legacy docs/re/MCX4_PRES_RE_ANSWERS_2.md Q4): refusal codes 1 / 2 / 5 -> Land_Retreat_Not_Allowed_SFXEvent /
// Space_Retreat_Not_Allowed_SFXEvent; 3 / 4 / 6 -> Land_Retreat_Not_Allowed_Reason_1_SFXEvent,
// Land_Retreat_Not_Allowed_Reason_2_SFXEvent, Land_Retreat_Not_Allowed_Reason_3_SFXEvent and the space
// Space_Retreat_Not_Allowed_Reason_1_SFXEvent, Space_Retreat_Not_Allowed_Reason_2_SFXEvent,
// Space_Retreat_Not_Allowed_Reason_3_SFXEvent.
// ledger: verifies Faction.Land_Retreat_Not_Allowed_SFXEvent
// ledger: verifies Faction.Space_Retreat_Not_Allowed_SFXEvent
// ledger: verifies Faction.Land_Retreat_Not_Allowed_Reason_1_SFXEvent
// ledger: verifies Faction.Land_Retreat_Not_Allowed_Reason_2_SFXEvent
// ledger: verifies Faction.Land_Retreat_Not_Allowed_Reason_3_SFXEvent
// ledger: verifies Faction.Space_Retreat_Not_Allowed_Reason_1_SFXEvent
// ledger: verifies Faction.Space_Retreat_Not_Allowed_Reason_2_SFXEvent
// ledger: verifies Faction.Space_Retreat_Not_Allowed_Reason_3_SFXEvent
void test_retreat_refusal_tag() {
    using H = GameAudioHost;
    CHECK(H::retreat_refusal_tag(true, 0).empty() && H::retreat_refusal_tag(false, 7).empty());
    for (const int32_t c : {1, 2, 5}) {
        CHECK(H::retreat_refusal_tag(true, c) == "Land_Retreat_Not_Allowed_SFXEvent");
        CHECK(H::retreat_refusal_tag(false, c) == "Space_Retreat_Not_Allowed_SFXEvent");
    }
    CHECK(H::retreat_refusal_tag(false, 3) == "Space_Retreat_Not_Allowed_Reason_1_SFXEvent");
    CHECK(H::retreat_refusal_tag(false, 4) == "Space_Retreat_Not_Allowed_Reason_2_SFXEvent");
    CHECK(H::retreat_refusal_tag(false, 6) == "Space_Retreat_Not_Allowed_Reason_3_SFXEvent");
    CHECK(H::retreat_refusal_tag(true, 3) == "Land_Retreat_Not_Allowed_Reason_1_SFXEvent");
    CHECK(H::retreat_refusal_tag(true, 4) == "Land_Retreat_Not_Allowed_Reason_2_SFXEvent");
    CHECK(H::retreat_refusal_tag(true, 6) == "Land_Retreat_Not_Allowed_Reason_3_SFXEvent");
}

void test_turret_turning() {
    using H = GameAudioHost;
    CHECK(!H::turret_turning(false, 0.0f, 0.0f, 10.0f, 0.0f));  // first tick seen: no baseline
    CHECK(H::turret_turning(true, 0.0f, 0.0f, 2.0f, 0.0f) && H::turret_turning(true, 0.0f, 0.0f, 0.0f, -1.0f));
    CHECK(!H::turret_turning(true, 5.0f, 1.0f, 5.0f, 1.0f));     // held: stopped
}

void test_command_rank() {
    using H = GameAudioHost;
    const std::vector<std::string> r =
        H::command_rankings(" Hero, Super, Capital, Frigate, Corvette, Transport, Bomber, Fighter, Air, Vehicle, Infantry, Structure ");
    CHECK(r.size() == 12 && r.front() == "Hero" && r.back() == "Structure");
    CHECK(H::command_rank(r, " Bomber | AntiCapital") == 6);
    CHECK(H::command_rank(r, " Fighter | AntiBomber") == 7);
    CHECK(H::command_rank(r, "infantry|hero") == 0);  // the first ranked category it names, any case
    CHECK(H::command_rank(r, "Unknown") == r.size() && H::command_rank(r, "") == r.size());
    CHECK(H::command_rank({}, "Hero") == 0);
}

void test_point_contested() {
    using H = GameAudioHost;
    CHECK(H::point_contested(true, 0, 0, 0.0f, 0.1f, 2));    // own point, progress starts, pending neutral
    CHECK(!H::point_contested(false, 0, 0, 0.0f, 0.1f, 2));  // not a REINFORCEMENT_POINT
    CHECK(!H::point_contested(true, 1, 0, 0.0f, 0.1f, 2));   // not the local player's
    CHECK(!H::point_contested(true, 0, 0, 0.2f, 0.3f, 2));   // already running: once per attempt
    CHECK(!H::point_contested(true, 0, 0, 0.0f, 0.1f, -1) && !H::point_contested(true, 0, 0, 0.0f, 0.1f, 0));
}

void test_tsw_approach() {
    using H = GameAudioHost;
    CHECK(H::tsw_threshold_crossed(61.0f, 59.0f) == 60 && H::tsw_threshold_crossed(31.0f, 29.5f) == 30);
    CHECK(H::tsw_threshold_crossed(70.0f, 10.0f) == 60);  // several in one step: the highest
    CHECK(H::tsw_threshold_crossed(5.5f, 4.0f) == 5 && H::tsw_threshold_crossed(59.0f, 40.0f) == 0);
    CHECK(H::tsw_speech_tag(60, false) == "SpeechEvent_Super_Weapon_Moving_Range_60_Seconds");
    CHECK(H::tsw_speech_tag(30, false) == "SpeechEvent_Super_Weapon_Moving_Range_30_Seconds");
    CHECK(H::tsw_speech_tag(15, false) == "SpeechEvent_Super_Weapon_Moving_Range_15_Seconds");
    CHECK(H::tsw_speech_tag(5, false) == "SpeechEvent_Super_Weapon_Moving_Range_05_Seconds");
    CHECK(H::tsw_speech_tag(60, true) == "SpeechEvent_Super_Weapon_Enemy_Moving_Range_60_Seconds");
    CHECK(H::tsw_speech_tag(30, true) == "SpeechEvent_Super_Weapon_Enemy_Moving_Range_30_Seconds");
    CHECK(H::tsw_speech_tag(15, true) == "SpeechEvent_Super_Weapon_Enemy_Moving_Range_15_Seconds");
    CHECK(H::tsw_speech_tag(5, true) == "SpeechEvent_Super_Weapon_Enemy_Moving_Range_05_Seconds");
    CHECK(H::tsw_speech_tag(0, false) == "SpeechEvent_Super_Weapon_Moved_Into_Range");
    CHECK(H::tsw_speech_tag(0, true) == "SpeechEvent_Super_Weapon_Enemy_Moved_Into_Range");
}

void test_pad_lines() {
    using H = GameAudioHost;
    const H::PadLines s = H::pad_lines(H::PadLine::Started, true);
    CHECK(s.type_tags[0] == "SFXEvent_Tactical_Build_Started" && s.type_tags[1] == "SFXEvent_Build_Started");
    CHECK(s.faction_tag == "SFXEvent_Tactical_Object_Building_Started" && s.loop_tag == "SFXEvent_Tactical_Object_Building_Loop");
    const H::PadLines r = H::pad_lines(H::PadLine::Started, false);  // another owner: faction lines only
    CHECK(r.type_tags[0].empty() && r.faction_tag == "SFXEvent_Tactical_Object_Building_Started" && !r.loop_tag.empty());
    const H::PadLines c = H::pad_lines(H::PadLine::Completed, true);
    CHECK(c.type_tags[0] == "SFXEvent_Tactical_Build_Complete" && c.faction_tag == "SFXEvent_Tactical_Object_Building_Complete");
    CHECK(c.loop_tag.empty());
    const H::PadLines d = H::pad_lines(H::PadLine::Sold, true);
    CHECK(d.type_tags[0] == "SFXEvent_Sold_Tactical" && d.type_tags[1].empty() && d.faction_tag == "SFXEvent_Tactical_Object_Sold");
    CHECK(H::pad_lines(H::PadLine::Sold, false).type_tags[0].empty());
}

void test_land_damage_stage() {
    using H = GameAudioHost;
    const std::vector<std::string_view> th = {"0.75", " 0.5", "0.25 "};
    CHECK(H::land_damage_stage(th, 1.0f) == -1 && H::land_damage_stage(th, 0.75f) == 0 && H::land_damage_stage(th, 0.6f) == 0);
    CHECK(H::land_damage_stage(th, 0.5f) == 1 && H::land_damage_stage(th, 0.1f) == 2);
    const std::vector<std::string_view> quirk = {"1"};  // thresholds "1": the stage holds at full health (TACTICAL_GAPS §6.3)
    CHECK(H::land_damage_stage(quirk, 1.0f) == 0 && H::land_damage_stage({}, 0.1f) == -1);
}

// aud4 b0289 / b0300: garrison entry line and the hero clash win speech (both builds; presentation rules only).
void test_garrison_and_clash_lines() {
    using H = GameAudioHost;
    CHECK(H::garrison_line(1, 1) == "SFXEvent_Bunker_Garrisoned");
    CHECK(H::garrison_line(2, 1).empty());   // an enemy / ally unit entering: no local line
    CHECK(H::garrison_line(-1, -1).empty());  // no local player
    tactical::combat::HeroClashData d;
    d.entries.resize(2);
    d.entries[1].first_speech = "Vader_Win";
    d.entries[1].second_speech = "Obiwan_Win";
    CHECK(H::clash_win_speech(&d, 1, true) == "Vader_Win");
    CHECK(H::clash_win_speech(&d, 1, false) == "Obiwan_Win");
    CHECK(H::clash_win_speech(&d, 0, true).empty());
    CHECK(H::clash_win_speech(&d, 5, true).empty() && H::clash_win_speech(nullptr, 0, true).empty());
}

void test_inactive() {
    GameData data;
    GameAudioHost h;
    CHECK(h.init(data, AudioBackendKind::Off, 1));
    CHECK(!h.active());
    h.movie_started(true, true);
    h.movie_cue("Movie_Cue");
    h.movie_ended();
    CHECK(h.stats().movie_music == 0 && h.stats().movie_cues == 0 && h.stats().movie_halts == 0);
}

// ccpgu t01 (SKIRMISH_SETUP 4.2 announcer): the countdown lines at 60 / 30 / 15 / 5 s (x 30 frames), by the leader's relation
// to the local player; none for a Neutral observer or while not armed. Both builds (the decision is presentation logic only).
void test_control_point_lines() {
    using H = GameAudioHost;
    const std::string_view owned30 = "SFXEvent_HUD_Reinforcement_Point_Owned_30_Seconds";
    // Arming at 900 (the shipped 30 s), seen as 900 or, when the first countdown step fell in the same tick, 899.
    CHECK(H::control_point_line(-1, 900, 1, 1, false) == owned30);
    CHECK(H::control_point_line(-1, 899, 1, 1, false) == owned30);
    CHECK(H::control_point_line(-1, 899, 2, 1, false) == "SFXEvent_HUD_Reinforcement_Point_Ally_Owned_30_Seconds");
    CHECK(H::control_point_line(-1, 899, 2, 1, true) == "SFXEvent_HUD_Reinforcement_Point_Enemy_Owned_30_Seconds");
    CHECK(H::control_point_line(-1, 899, 2, -1, true).empty());  // no local player
    CHECK(H::control_point_line(5, -1, -1, 1, false).empty());   // disarmed
    // Counting down: each mark once, on the tick that reaches it.
    CHECK(H::control_point_line(899, 898, 1, 1, false).empty());
    CHECK(H::control_point_line(451, 450, 1, 1, false) == "SFXEvent_HUD_Reinforcement_Point_Owned_15_Seconds");
    CHECK(H::control_point_line(450, 449, 1, 1, false).empty());
    CHECK(H::control_point_line(151, 150, 2, 1, true) == "SFXEvent_HUD_Reinforcement_Point_Enemy_Owned_05_Seconds");
    CHECK(H::control_point_line(1801, 1800, 1, 1, false) == "SFXEvent_HUD_Reinforcement_Point_Owned_60_Seconds");
    // A re-arm (900 again after a capture) plays the 30 s line again.
    CHECK(H::control_point_line(300, 900, 1, 1, false) == owned30);
    // Over the shipped 30 s countdown exactly three lines: 30 s at arming, 15 s, 5 s.
    int lines = 0, prev = -1;
    for (int t = 900; t >= 0; --t) {
        lines += H::control_point_line(prev, t, 1, 1, false).empty() ? 0 : 1;
        prev = t;
    }
    CHECK(lines == 3);
}

// aud1 t07 (legacy docs/re/AUDIO.md §9.7): the tactical intro speech pick, its gate, and (with engine/audio) one queued line.
// ledger: verifies Faction.SpeechEvent_Tactical_Intro_Land_Attacker
// ledger: verifies Faction.SpeechEvent_Tactical_Intro_Land_Defender
// ledger: verifies Faction.SpeechEvent_Tactical_Intro_Space_Attacker
// ledger: verifies Faction.SpeechEvent_Tactical_Intro_Space_Defender
// ledger: verifies Faction.SpeechEvent_Tactical_Intro_Land_Raid_Attacker
// ledger: verifies Faction.SpeechEvent_Tactical_Intro_Land_Raid_Defender
void test_intro_speech_t07() {
    using H = GameAudioHost;
    using P = std::pair<std::string, std::string>;
    const std::vector<P> rebel = {  // shaped like the shipped Factions.xml Rebel entries
        {"SpeechEvent_Tactical_Intro_Land_Attacker", "LAND_ATT"},
        {"SpeechEvent_Tactical_Intro_Land_Defender", "LAND_DEF"},
        {"SpeechEvent_Tactical_Intro_Space_Attacker", "SPACE_ATT, SPACE_ATT_ALT"},
        {"SpeechEvent_Tactical_Intro_Space_Defender", "SPACE_DEF"},
        {"SpeechEvent_Tactical_Intro_Land_Raid_Attacker", "RAID_ATT"},
        {"SpeechEvent_Tactical_Intro_Land_Raid_Defender", ""},
        {"SpeechEvent_Tactical_Intro_Land_Attacker_Last_Location", "LAST_ATT"},
        {"SpeechEvent_Tactical_Intro_Land_Defender_Last_Location", "LAST_DEF"},
        {"SpeechEvent_Tactical_Intro_Space_Defender_Conditional_And", "Death_Star, Luke_Skywalker, None, AND_DS_LUKE"},
        {"SpeechEvent_Tactical_Intro_Space_Defender_Conditional_Or", "Death_Star, None, Luke_Skywalker, OR_DS_NO_LUKE"},
        {"SpeechEvent_Tactical_Intro_Space_Defender_Conditional_Or", "Mon_Mothma, None, None, OR_MOTHMA"},
    };
    std::vector<std::string> here;
    const std::function<bool(std::string_view)> present = [&here](std::string_view n) {
        for (const std::string& s : here)
            if (s == n) return true;
        return false;
    };
    H::IntroSpeechFacts f;
    // Defaults by side and map; a multi-event value speaks its first event [port].
    f.land = true;
    f.attacker = true;
    CHECK(H::intro_speech_pick(rebel, f, present) == "LAND_ATT");
    f.attacker = false;
    CHECK(H::intro_speech_pick(rebel, f, present) == "LAND_DEF");
    f.land = false;
    f.attacker = true;
    CHECK(H::intro_speech_pick(rebel, f, present) == "SPACE_ATT");
    f.attacker = false;
    CHECK(H::intro_speech_pick(rebel, f, present) == "SPACE_DEF");
    // Conditional lists: Or before And (the first match wins), C absent required, "None" never present.
    here = {"Death_Star"};
    CHECK(H::intro_speech_pick(rebel, f, present) == "OR_DS_NO_LUKE");
    here = {"Death_Star", "Luke_Skywalker"};
    CHECK(H::intro_speech_pick(rebel, f, present) == "AND_DS_LUKE");
    here = {"Death_Star", "Luke_Skywalker", "Mon_Mothma"};
    CHECK(H::intro_speech_pick(rebel, f, present) == "OR_MOTHMA");  // the Or list is searched completely first
    here = {"Luke_Skywalker"};
    CHECK(H::intro_speech_pick(rebel, f, present) == "SPACE_DEF");
    f.attacker = true;  // the defender's lists are not the attacker's
    here = {"Mon_Mothma"};
    CHECK(H::intro_speech_pick(rebel, f, present) == "SPACE_ATT");
    here.clear();
    // Land: last location before raid before the default; space ignores both.
    f.land = true;
    f.last_location = true;
    f.raid = true;
    CHECK(H::intro_speech_pick(rebel, f, present) == "LAST_ATT");
    f.last_location = false;
    CHECK(H::intro_speech_pick(rebel, f, present) == "RAID_ATT");
    f.attacker = false;  // an empty raid field falls through to the default
    CHECK(H::intro_speech_pick(rebel, f, present) == "LAND_DEF");
    f.land = false;
    f.last_location = true;
    CHECK(H::intro_speech_pick(rebel, f, present) == "SPACE_DEF");
    // The land attacker default needs a playable defender; the other defaults do not.
    f = {};
    f.land = true;
    f.attacker = true;
    f.defender_playable = false;
    CHECK(H::intro_speech_pick(rebel, f, present).empty());
    f.raid = true;
    CHECK(H::intro_speech_pick(rebel, f, present) == "RAID_ATT");
    f.raid = false;
    f.attacker = false;
    CHECK(H::intro_speech_pick(rebel, f, present) == "LAND_DEF");
    CHECK(H::intro_speech_pick({}, f, present).empty());  // a faction without the fields says nothing

    // Gate: 10 s x 30 fps of battle frames, no cinematic, no speech; never outside a campaign / in a linked battle.
    CHECK(H::intro_speech_gate(false, 1000, false, false) == H::IntroGate::Stop);
    CHECK(H::intro_speech_gate(true, 299, false, false) == H::IntroGate::Wait);
    CHECK(H::intro_speech_gate(true, 300, false, false) == H::IntroGate::Speak);
    CHECK(H::intro_speech_gate(true, 300, true, false) == H::IntroGate::Wait);
    CHECK(H::intro_speech_gate(true, 300, false, true) == H::IntroGate::Wait);

    // The tick: decided once per battle; a lower frame starts a new battle. Inactive host: nothing queued.
    {
        GameAudioHost idle;
        int asked = 0;
        const auto facts = [&asked](H::IntroSpeechFacts& x) {
            ++asked;
            x.land = false;
            return true;
        };
        CHECK(idle.intro_speech_tick(10, true, false, facts, present).empty() && asked == 0);
        CHECK(idle.intro_speech_tick(300, true, false, facts, present).empty() && asked == 1);
        CHECK(idle.intro_speech_tick(301, true, false, facts, present).empty() && asked == 1);
        CHECK(idle.intro_speech_tick(0, true, false, facts, present).empty() && asked == 1);
        CHECK(idle.intro_speech_tick(300, true, false, facts, present).empty() && asked == 2);
    }
#ifdef ALAMO_GAME_HAVE_AUDIO
    {
        GameData data;
        auto tree = make_tree(true);
        tree->add("Data\\XML\\SpeechEvents.xml",
                  "<SpeechEvents><SpeechEvent Name=\"GRS_INTRO_SPACE_DEFEND_R_00\"><Files> I_ENG.MP3 </Files></SpeechEvent>"
                  "</SpeechEvents>");
        tree->add("Data\\XML\\Factions.xml",
                  "<Factions><Faction Name=\"Rebel\"><Strategic_Map_Music_Event> Gal_Rebel </Strategic_Map_Music_Event>"
                  "<SpeechEvent_Tactical_Intro_Space_Defender> GRS_INTRO_SPACE_DEFEND_R_00 </SpeechEvent_Tactical_Intro_Space_Defender>"
                  "</Faction></Factions>");
        data.files = std::move(tree);
        GameAudioHost h;
        std::string err;
        CHECK(h.init(data, AudioBackendKind::Null, 7, &err));
        if (!h.active()) return;
        h.music(MusicScene::Galactic, "Rebel");
        const auto defender = [](H::IntroSpeechFacts& x) {
            x.land = false;
            x.attacker = false;
            return true;
        };
        const uint64_t before = h.stats().speech;
        CHECK(h.intro_speech_tick(299, true, false, defender, present).empty());
        CHECK(h.intro_speech_tick(300, true, false, defender, present) == "GRS_INTRO_SPACE_DEFEND_R_00");
        CHECK(h.stats().speech == before + 1);
        CHECK(h.intro_speech_tick(400, true, false, defender, present).empty() && h.stats().speech == before + 1);
        // Not the local player's battle side: decided, silent.
        CHECK(h.intro_speech_tick(0, true, false, [](H::IntroSpeechFacts&) { return false; }, present).empty());
    }
#endif
}


#ifdef ALAMO_GAME_HAVE_AUDIO
// aud1 (legacy docs/re/AUDIO.md §9): a scripted land battle (Temperate, rain) and a space battle through GameAudioHost - ambient then
// battle music from the map terrain's list, the weather loop and SFXEvent_Weather_Begin, an AnimationSFXMaps SOUND row,
// a hardpoint Fire_SFXEvent, the base-attack line (rate-limited), and TSW / Lua Play_Music not replaced by combat.
const char* kAud1Sfx = R"(<?xml version="1.0"?>
<SFXEvents>
  <SFXEvent Name="Rain_Line"><Is_2D>Yes</Is_2D><Is_3D>No</Is_3D><Samples>RAIN_LINE.WAV</Samples></SFXEvent>
  <SFXEvent Name="Rain_Loop"><Is_2D>Yes</Is_2D><Is_3D>No</Is_3D><Play_Count>-1</Play_Count><Samples>RAIN.WAV</Samples></SFXEvent>
  <SFXEvent Name="Base_Line"><Is_2D>Yes</Is_2D><Is_3D>No</Is_3D><Samples>BASE.WAV</Samples></SFXEvent>
  <SFXEvent Name="Anim_Stomp"><Samples>STOMP.WAV</Samples><Max_Instances>4</Max_Instances></SFXEvent>
  <SFXEvent Name="HP_Turbolaser"><Samples>TL.WAV</Samples><Max_Instances>8</Max_Instances></SFXEvent>
  <SFXEvent Name="Ship_Fire"><Samples>SF.WAV</Samples><Max_Instances>8</Max_Instances></SFXEvent>
  <SFXEvent Name="Hyper_Arrive"><Samples>HYPER.WAV</Samples><Max_Instances>8</Max_Instances></SFXEvent>
  <SFXEvent Name="GUI_Audio_Options_SFX_Test"><Is_2D>Yes</Is_2D><Is_3D>No</Is_3D><Samples>SFXTEST.WAV</Samples></SFXEvent>
  <SFXEvent Name="SFX_Stealth_Reveal"><Is_2D>No</Is_2D><Is_3D>Yes</Is_3D><Samples>REVEAL.WAV</Samples><Max_Instances>4</Max_Instances></SFXEvent>
</SFXEvents>
)";
const char* kAud1Music = R"(<?xml version="1.0"?>
<MusicEvents>
  <MusicEvent Name="Amb_Temperate"><Files>AT.MP3</Files><Loop>Yes</Loop></MusicEvent>
  <MusicEvent Name="Bat_Temperate"><Files>BT.MP3</Files><Loop>Yes</Loop></MusicEvent>
  <MusicEvent Name="Amb_Desert"><Files>AD.MP3</Files><Loop>Yes</Loop></MusicEvent>
  <MusicEvent Name="Amb_Space"><Files>AS.MP3</Files><Loop>Yes</Loop></MusicEvent>
  <MusicEvent Name="Bat_Space"><Files>BS.MP3</Files><Loop>Yes</Loop></MusicEvent>
  <MusicEvent Name="TSW_Song"><Files>TSW.MP3</Files></MusicEvent>
  <MusicEvent Name="Lua_Song"><Files>LUA.MP3</Files></MusicEvent>
</MusicEvents>
)";
const char* kAud1Factions = R"(<?xml version="1.0" ?>
<Factions>
  <Faction Name="Rebel">
    <Music_Event_List_Ambient> Space, Amb_Space </Music_Event_List_Ambient>
    <Music_Event_List_Ambient> Temperate, Amb_Temperate </Music_Event_List_Ambient>
    <Music_Event_List_Ambient> Desert, Amb_Desert </Music_Event_List_Ambient>
    <Music_Event_List_Battle> Space, Bat_Space </Music_Event_List_Battle>
    <Music_Event_List_Battle> Temperate, Bat_Temperate </Music_Event_List_Battle>
    <SFXEvent_Weather_Begin> RAIN, Rain_Line </SFXEvent_Weather_Begin>
    <SFXEvent_Land_Base_Under_Attack_Announcement> Base_Line </SFXEvent_Land_Base_Under_Attack_Announcement>
    <SFXEvent_Arrive_From_Hyperspace> Hyper_Arrive </SFXEvent_Arrive_From_Hyperspace>
  </Faction>
</Factions>
)";
const char* kAud1Weather = R"(<?xml version="1.0" ?>
<WeatherAudio>
  <Weather_SFXEvent_Loop> CLEAR, 0.00, None </Weather_SFXEvent_Loop>
  <Weather_SFXEvent_Loop> RAIN, 0.01, Rain_Loop </Weather_SFXEvent_Loop>
  <Weather_SFXEvent_Loop> RAIN, 0.00, None </Weather_SFXEvent_Loop>
</WeatherAudio>
)";

int starts(GameAudioHost& h, std::string_view ev) {
    int n = 0;
    for (const audio::LogEntry& e : h.system()->log()) n += e.op == audio::LogOp::SfxStart && e.event == ev ? 1 : 0;
    return n;
}

// #225 (retail answer on #205, 0x2d4b00): an attached 3D sound is muted for a Lua-hidden object (also during a cinematic,
// `hidden`) or a fogged one (`inaudible` only: the cinematic camera unmutes it). Stealth is no reason: a stealthed object
// is heard unless it is fogged for the local player.
void test_object_mute() {
    using H = GameAudioHost;
    H::ObjectMute m = H::object_mute(true, false, false);  // stealthed, not fogged
    CHECK(!m.inaudible && !m.hidden);
    m = H::object_mute(true, false, true);  // stealthed and fogged: muted as fogged
    CHECK(m.inaudible && !m.hidden);
    m = H::object_mute(false, true, false);  // Lua Hide
    CHECK(m.inaudible && m.hidden);
    m = H::object_mute(false, false, false);
    CHECK(!m.inaudible && !m.hidden);
}

void test_battle_hookups_aud1() {
    using H = GameAudioHost;
    // Pure rules first: terrain keys (map mini 06, §9.1), fire event choice (§9.5), base-attack test (§9.8).
    CHECK(H::terrain_name(0) == "Temperate" && H::terrain_name(6) == "Urban" && H::terrain_name(7) == "Space");
    CHECK(H::terrain_name(8).empty());
    CHECK(H::fire_event("HP_Turbolaser", "Ship_Fire") == "HP_Turbolaser" && H::fire_event("", "Ship_Fire") == "Ship_Fire");
    CHECK(H::fire_event("", "").empty());
    CHECK(H::base_attacked(true, true, 1, 1, false, false));
    CHECK(!H::base_attacked(false, true, 1, 1, false, false));  // not a base object (a unit, a plain turret)
    CHECK(!H::base_attacked(true, false, 1, 1, false, false));  // attacker not hostile
    CHECK(!H::base_attacked(true, true, 2, 1, true, false));    // an ally's base without Is_Community_Property
    CHECK(H::base_attacked(true, true, 2, 1, true, true));      // ... with it
    CHECK(!H::base_attacked(true, true, 3, 1, false, true));    // an enemy's base
    CHECK(H::weather_name(1) == "RAIN" && H::weather_name(0) == "CLEAR" && H::weather_name(9) == "CLEAR");

    auto tree = std::make_unique<MemoryProvider>();
    tree->add("Data\\XML\\SFXEventFiles.xml", "<SFXEvent_Files><File>SFXEventsAud1.xml</File></SFXEvent_Files>");
    tree->add("Data\\XML\\SFXEventsAud1.xml", kAud1Sfx);
    tree->add("Data\\XML\\MusicEvents.xml", kAud1Music);
    tree->add("Data\\XML\\SpeechEvents.xml", "<SpeechEvents/>");
    tree->add("Data\\XML\\Audio.xml",
              "<Audio><Delay_Between_Land_Base_Attack_Announcement_Seconds> 60 </Delay_Between_Land_Base_Attack_Announcement_Seconds>"
              "<Music_Land_Battle_To_Ambient_Peace_Seconds> 15.0 </Music_Land_Battle_To_Ambient_Peace_Seconds></Audio>");
    tree->add("Data\\XML\\FactionFiles.xml", "<Faction_Files><File>Factions.xml</File></Faction_Files>");
    tree->add("Data\\XML\\Factions.xml", kAud1Factions);
    tree->add("Data\\XML\\WeatherAudio.xml", kAud1Weather);
    GameData data;
    data.files = std::move(tree);
    GameAudioHost h;
    CHECK(h.init(data, AudioBackendKind::Null, 11));
    if (!h.active()) return;
    const Vec3 p{10.0f, 20.0f, 0.0f};
    h.set_object_positions({{5, p}, {6, p}});

    // Land battle, Temperate map: ambient music from the Temperate row (mode 6).
    h.music(MusicScene::Battle, "Rebel", false, H::terrain_name(0));
    CHECK(cur(h) == "Amb_Temperate" && mode(h) == audio::music_mode::kTacticalAmbient);
    // Rain starts: the faction line once (intensity 0 -> > 0), the weather loop on the next update.
    h.weather("RAIN", 0.6f);
    h.weather("RAIN", 0.8f);
    CHECK(starts(h, "Rain_Line") == 1);
    h.update(0.05);
    CHECK(starts(h, "Rain_Loop") == 1);
    // An AnimationSFXMaps SOUND row, attached to its object.
    const H::AnimSound stomp[] = {{5, "Anim_Stomp", p}};
    CHECK(h.animation_sounds(stomp) == 1 && h.stats().anim_sounds == 1);
    // A hardpoint shot plays the hardpoint's Fire_SFXEvent, not the ship's SFXEvent_Fire.
    CHECK(h.weapon_fired(6, "HP_Turbolaser", "Ship_Fire", p) != 0);
    CHECK(starts(h, "HP_Turbolaser") == 1 && starts(h, "Ship_Fire") == 0);
    // Combat involving the local player: battle music from the Temperate row (mode 7).
    h.combat(true, false);
    CHECK(cur(h) == "Bat_Temperate" && mode(h) == audio::music_mode::kTacticalBattle);
    // Base under attack: the faction line, then nothing for 60 s x 30 ticks.
    h.combat(false, true);
    CHECK(starts(h, "Base_Line") == 1);
    h.update(2.0);
    h.combat(false, true);
    CHECK(starts(h, "Base_Line") == 1);
    for (int t = 0; t < 1800; ++t) h.sim_tick();  // also past the 15 s peace timeout: back to ambient
    CHECK(cur(h) == "Amb_Temperate");
    h.update(2.0);  // the first line's instance ends (Max_Instances 1)
    h.update(2.0);
    h.combat(false, true);
    CHECK(starts(h, "Base_Line") == 2);
    // TSW activation music (mode 8) and Lua Play_Music (mode 12) are not replaced by combat (§9.2).
    h.system()->play_music("TSW_Song", audio::music_mode::kSuperWeapon, false, false);
    h.combat(true, false);
    CHECK(cur(h) == "TSW_Song" && mode(h) == audio::music_mode::kSuperWeapon);
    h.system()->play_music("Lua_Song", audio::music_mode::kLua, false, true);
    h.combat(true, false);
    for (int t = 0; t < 900; ++t) h.sim_tick();  // no peace timeout out of mode 12 either
    CHECK(cur(h) == "Lua_Song" && mode(h) == audio::music_mode::kLua);

    // Another land terrain picks its own row.
    h.music(MusicScene::None);
    h.music(MusicScene::Battle, "Rebel", false, H::terrain_name(2));
    CHECK(cur(h) == "Amb_Desert");

    // Space battle: the Space row, ambient then battle.
    h.music(MusicScene::None);
    h.music(MusicScene::Battle, "Rebel", true, H::terrain_name(7));
    CHECK(cur(h) == "Amb_Space" && mode(h) == audio::music_mode::kTacticalAmbient);
    h.combat(true, false);
    CHECK(cur(h) == "Bat_Space" && mode(h) == audio::music_mode::kTacticalBattle);
    // A hyperspace arrival: the owner faction's SFXEvent_Arrive_From_Hyperspace, attached to the ship (§9.6).
    CHECK(h.hyperspace_arrival(6, "Rebel", p) != 0 && starts(h, "Hyper_Arrive") == 1 && h.hyperspace_sfx_played() == 1);
    CHECK(h.hyperspace_arrival(6, "", p) == 0);  // no owner faction: nothing
    // #67 t02 (legacy docs/re/ABILITY_VISUALS.md §1.4, 0x592ae0 [H]): a stealthed unit revealed by detection plays
    // SFX_Stealth_Reveal by name, 2D (0x2d5010), whoever owns it. The fixture is 3D-only like the shipped event
    // (Preset_EXPC): the by-name 2D entry plays it 2D anyway.
    CHECK(h.stealth_reveal() != 0 && starts(h, "SFX_Stealth_Reveal") == 1 && h.stats().stealth_reveals == 1);
    // #62 t01 (0x81c20 [H]): the options dialog's SFX test plays GUI_Audio_Options_SFX_Test by name, 2D; the speech test
    // streams Test_Speech_ENG.MP3 (localized) on the speech bus.
    CHECK(h.options_sfx_test() != 0 && starts(h, "GUI_Audio_Options_SFX_Test") == 1);
    h.options_speech_test();  // the stream is requested (the test tree has no file: the null backend reports it missing)
    {
        bool streamed = false;
        for (const audio::LogEntry& e : h.system()->log()) streamed = streamed || (e.op == audio::LogOp::SpeechStart && e.sample == "Test_Speech_ENG.MP3");
        CHECK(streamed);
    }
}
#endif

// ledger: verifies Laser_Defense_Ability.Zap_SFXEvent
// ledger: verifies Tractor_Beam_Attack_Ability.SFXEvent_Unit_Summoned
// ledger: verifies Radioactive_Contaminate_Ability.SFXEvent_Radioactive
// ledger: verifies Buzz_Droids_Ability.Activate_SFX
// ledger: verifies Tactical_Bribe_Ability.SFXEvent_Success
// ledger: verifies Tactical_Bribe_Ability.SFXEvent_Fail
// ledger: verifies Unit_Ability.SFXEvent_GUI_Unit_Ability_Activated
// ledger: verifies Unit_Ability.SFXEvent_GUI_Unit_Ability_Deactivated
// ledger: verifies Unit_Ability.SFXEvent_Target_Ability
// ledger: verifies Faction.SFXEvent_GUI_Toggle_Non_Hero_Ability_On
// ledger: verifies Faction.SFXEvent_GUI_Toggle_Non_Hero_Ability_Off
// ledger: verifies Faction.SFXEvent_GUI_Enemy_Toggle_Non_Hero_Ability_On
// ledger: verifies Faction.SFXEvent_GUI_Enemy_Toggle_Non_Hero_Ability_Off
// #62 t06 (the activation presentation 0x48e4d0 [H]): which galactic applies present it, the tags by ability name, and (with
// engine/audio) the activation on the hero and the target sound on the planet.
void test_galactic_activate_t06() {
    using H = GameAudioHost;
    using C = galactic::SpecialAbilityClass;
    for (const bool local : {false, true}) {
        CHECK(H::galactic_activation_presented(C::Sabotage, local) && H::galactic_activation_presented(C::Hack, local) &&
              H::galactic_activation_presented(C::SystemSpy, local) && H::galactic_activation_presented(C::SiphonCredits, local));
        CHECK(!H::galactic_activation_presented(C::BlackMarket, local) && !H::galactic_activation_presented(C::Slicer, local) &&
              !H::galactic_activation_presented(C::Corruption, local) && !H::galactic_activation_presented(C::NeutralizeHero, local) &&
              !H::galactic_activation_presented(C::PlanetDestruction, local) && !H::galactic_activation_presented(C::Other, local));
    }
    CHECK(H::galactic_activation_presented(C::RemoveCorruption, true) && !H::galactic_activation_presented(C::RemoveCorruption, false));
    // One batch (local player 0): ability refs 0..4 = Sabotage, RemoveCorruption, Corruption, SystemSpy, Hack.
    {
        using K = galactic::GalacticEventKind;
        const auto class_of = [](uint32_t ref) {
            constexpr C kClass[] = {C::Sabotage, C::RemoveCorruption, C::Corruption, C::SystemSpy, C::Hack};
            return ref < 5 ? kClass[ref] : C::Other;
        };
        auto ev = [](K k, int32_t player, int32_t hero, int32_t ability, uint32_t planet) {
            galactic::GalacticEvent e;
            e.kind = k;
            e.player = player;
            e.object = hero;
            e.value = ability;
            e.type = 7;
            e.planet = planet;
            return e;
        };
        const std::vector<galactic::GalacticEvent> batch = {
            ev(K::SpecialApplied, 1, 10, 0, 2),     // the AI's sabotage: presents
            ev(K::SabotageVigilance, 0, 11, 0, 3),  // the local saboteur 11 caught by vigilance ...
            ev(K::SpecialApplied, 0, 11, 0, 3),     // ... its "applied" sabotage does not present
            ev(K::SpecialApplied, 1, 12, 1, 4),     // the AI's Remove_Corruption: not presented
            ev(K::SpecialApplied, 0, 13, 1, 4),     // the local one: presents
            ev(K::SpecialApplied, 1, 14, 2, 5),     // Corruption_Ability: never
            ev(K::SpecialApplied, 1, 15, 3, 6),     // the AI's system spy: presents
            ev(K::SpecialError, 0, 16, 4, 6),       // other kinds: ignored
        };
        const std::vector<H::GalacticActivation> a = H::galactic_activations(batch, class_of, 0);
        CHECK(a.size() == 3);
        if (a.size() == 3) {
            CHECK(a[0].hero == 10 && a[0].ability == 0 && a[0].planet == 2 && a[0].type == 7);
            CHECK(a[1].hero == 13 && a[1].ability == 1 && a[1].planet == 4);
            CHECK(a[2].hero == 15 && a[2].ability == 3 && a[2].planet == 6);
        }
        CHECK(H::galactic_activations(batch, nullptr, 0).empty());
    }

    MemoryProvider files;
    files.add("Data\\XML\\GameObjectFiles.xml", "<L><File>T06.xml</File></L>");
    files.add("Data\\XML\\T06.xml", R"(<?xml version="1.0"?>
<SpaceUnits>
  <SpaceUnit Name="Gal_Hero">
    <Abilities SubObjectList="Yes">
      <Remove_Corruption_Ability Name="Clean"><SFXEvent_Activate> Clean_Line </SFXEvent_Activate></Remove_Corruption_Ability>
      <Galactic_Sabotage_Ability Name="Sab"><SFXEvent_Activate>Sab_Line</SFXEvent_Activate>
        <SFXEvent_Target_Affected>Sab_Target</SFXEvent_Target_Affected></Galactic_Sabotage_Ability>
    </Abilities>
  </SpaceUnit>
</SpaceUnits>
)");
    objects::ObjectTypeDatabase raw;
    CHECK(raw.load(files));
    objects::GameObjectTypes types;
    CHECK(types.build(raw));
    const objects::TypeIndex hero = types.resolve_name("Gal_Hero");
    CHECK(hero != objects::kNone);
    if (hero == objects::kNone) return;
    H::AbilitySfx s = H::ability_sfx_named(types, hero, "sab");
    CHECK(s.activate == "Sab_Line" && s.target_affected == "Sab_Target");
    s = H::ability_sfx_named(types, hero, "Clean");
    CHECK(s.activate == "Clean_Line" && s.target_affected.empty());
    CHECK(H::ability_sfx_named(types, hero, "Other").activate.empty() &&
          H::ability_sfx_named(types, objects::kNone, "Sab").activate.empty());
    GameAudioHost off;  // never initialised: no-op
    CHECK(off.galactic_ability_effect(types, hero, "Sab", 5, 9, Vec3{}) == 0);
#ifdef ALAMO_GAME_HAVE_AUDIO
    GameData data;
    auto tree = make_tree(true);
    tree->add("Data\\XML\\SFXEventsTest.xml", R"(<?xml version="1.0"?>
<SFXEvents>
  <SFXEvent Name="Sab_Line"><Is_2D>Yes</Is_2D><Is_3D>No</Is_3D><Max_Instances>2</Max_Instances><Samples>SAB.WAV</Samples></SFXEvent>
  <SFXEvent Name="Sab_Target"><Is_3D>Yes</Is_3D><Samples>SABT.WAV</Samples></SFXEvent>
  <SFXEvent Name="Clean_Line"><Is_2D>Yes</Is_2D><Is_3D>No</Is_3D><Samples>CLEAN.WAV</Samples></SFXEvent>
</SFXEvents>
)");
    data.files = std::move(tree);
    GameAudioHost h;
    CHECK(h.init(data, AudioBackendKind::Null, 3));
    if (!h.active()) return;
    h.set_listener(Vec3{0, 0, 100}, Vec3{0, 0, 0});
    CHECK(h.galactic_ability_effect(types, hero, "Sab", 5, 9, Vec3{10, 0, 0}) == 2);
    bool user = false, target = false;
    for (const audio::LogEntry& e : h.system()->log()) {
        if (e.op != audio::LogOp::SfxStart) continue;
        user = user || (e.event == "Sab_Line" && e.owner == 5);
        target = target || (e.event == "Sab_Target" && e.owner == 9);
    }
    CHECK(user && target);
    CHECK(h.galactic_ability_effect(types, hero, "Sab", 5, 0, Vec3{10, 0, 0}) == 1);  // no planet object: the activation only
    CHECK(h.galactic_ability_effect(types, hero, "Clean", 5, 9, Vec3{}) == 1 && starts(h, "Clean_Line") == 1);
    CHECK(h.galactic_ability_effect(types, hero, "Other", 5, 9, Vec3{}) == 0);
#endif
}

// #50 t01 (0x39bdb0 [H]): a local unit's death plays its SFXEvent_Unit_Lost (2D); another player's does not. t03: the
// sighting lines (a Play_SFXEvent_On_Sighting type once per battle, else the first enemy sighting once per battle).
void test_unit_lost_sighting_50() {
    using H = GameAudioHost;
    MemoryProvider files;
    files.add("Data\\XML\\GameObjectFiles.xml", "<L><File>T50.xml</File></L>");
    files.add("Data\\XML\\T50.xml", R"(<?xml version="1.0"?>
<GroundVehicles>
  <GroundVehicle Name="Tank"><SFXEvent_Unit_Lost> Tank_Lost </SFXEvent_Unit_Lost></GroundVehicle>
  <GroundVehicle Name="Hero_Tank"><Play_SFXEvent_On_Sighting> Yes </Play_SFXEvent_On_Sighting></GroundVehicle>
</GroundVehicles>
)");
    objects::ObjectTypeDatabase raw;
    CHECK(raw.load(files));
    objects::GameObjectTypes types;
    CHECK(types.build(raw));
    const objects::TypeIndex tank = types.resolve_name("Tank"), hero = types.resolve_name("Hero_Tank");
    CHECK(tank != objects::kNone && hero != objects::kNone);
    if (tank == objects::kNone || hero == objects::kNone) return;
    H off;  // inactive host: nothing
    CHECK(off.unit_died(types, 5, tank, Vec3{}, true) == 0 && !off.unit_sighted(types, tank, true));
#ifdef ALAMO_GAME_HAVE_AUDIO
    auto tree = make_tree(true);
    tree->add("Data\\XML\\SFXEventsTest.xml", R"(<?xml version="1.0"?>
<SFXEvents>
  <SFXEvent Name="Tank_Lost"><Is_2D>Yes</Is_2D><Is_3D>No</Is_3D><Max_Instances>4</Max_Instances><Samples>LOST.WAV</Samples></SFXEvent>
  <SFXEvent Name="Enemy_Seen"><Is_2D>Yes</Is_2D><Is_3D>No</Is_3D><Max_Instances>4</Max_Instances><Samples>SEEN.WAV</Samples></SFXEvent>
  <SFXEvent Name="Hero_Seen"><Is_2D>Yes</Is_2D><Is_3D>No</Is_3D><Max_Instances>4</Max_Instances><Samples>HERO.WAV</Samples></SFXEvent>
</SFXEvents>
)");
    tree->add("Data\\XML\\Factions.xml", R"(<?xml version="1.0" ?>
<Factions>
  <Faction Name="Rebel"><SFXEvent_Enemy_Spotted> Enemy_Seen </SFXEvent_Enemy_Spotted>
    <SFXEvent_Unit_Type_Spotted> Hero_Tank, Hero_Seen </SFXEvent_Unit_Type_Spotted></Faction>
</Factions>
)");
    GameData data;
    data.files = std::move(tree);
    H h;
    CHECK(h.init(data, AudioBackendKind::Null, 3));
    if (!h.active()) return;
    h.music(MusicScene::Galactic, "Rebel");  // the local faction
    // t01: another player's unit: its death line only; the local player's: SFXEvent_Unit_Lost too (2D).
    h.unit_died(types, 5, tank, Vec3{10, 0, 0}, false);
    CHECK(starts(h, "Tank_Lost") == 0 && h.stats().unit_lost_lines == 0);
    h.unit_died(types, 6, tank, Vec3{10, 0, 0}, true);
    CHECK(starts(h, "Tank_Lost") == 1 && h.stats().unit_lost_lines == 1);
    h.unit_died(types, 7, hero, Vec3{10, 0, 0}, true);  // no Unit_Lost tag: nothing more
    CHECK(h.stats().unit_lost_lines == 1);
    // t03: an ally's ordinary unit says nothing; the enemy's first sighting says Enemy_Spotted once per battle.
    CHECK(!h.unit_sighted(types, tank, false));
    CHECK(h.unit_sighted(types, tank, true) && starts(h, "Enemy_Seen") == 1);
    CHECK(!h.unit_sighted(types, tank, true) && starts(h, "Enemy_Seen") == 1);
    // a Play_SFXEvent_On_Sighting type: its own line once per battle (an ally's too), then like any other unit.
    CHECK(h.unit_sighted(types, hero, false) && starts(h, "Hero_Seen") == 1);
    CHECK(!h.unit_sighted(types, hero, true) && starts(h, "Hero_Seen") == 1 && starts(h, "Enemy_Seen") == 1);
    CHECK(h.stats().sighting_lines == 2);
    h.battle_reset();  // a new battle: both announce again
    CHECK(h.unit_sighted(types, hero, true) && starts(h, "Hero_Seen") == 2);
    CHECK(h.unit_sighted(types, tank, true) && starts(h, "Enemy_Seen") == 2);
#endif
}

// aud1 t05 (legacy docs/re/AUDIO.md §9.6): the ability tag pick through the type's Unit_Ability -> <Abilities> entry, the departure tag
// pick, the shield transitions, and (with engine/audio) the activation playing on the user and the target.
void test_object_sfx_t05() {
    using H = GameAudioHost;
    MemoryProvider files;
    files.add("Data\\XML\\GameObjectFiles.xml", "<L><File>T05.xml</File></L>");
    files.add("Data\\XML\\T05.xml", R"(<?xml version="1.0"?>
<SpaceUnits>
  <SpaceUnit Name="Hero_Ship">
    <Unit_Abilities_Data SubObjectList="Yes">
      <Unit_Ability><Type>STEALTH</Type><GUI_Activated_Ability_Name> Hide </GUI_Activated_Ability_Name>
        <SFXEvent_GUI_Unit_Ability_Activated> Gui_Stealth_On </SFXEvent_GUI_Unit_Ability_Activated></Unit_Ability>
      <Unit_Ability><Type>SPRINT</Type><SFXEvent_GUI_Unit_Ability_Deactivated>Gui_Sprint_Off</SFXEvent_GUI_Unit_Ability_Deactivated>
        <SFXEvent_Target_Ability>Sprint_Target</SFXEvent_Target_Ability></Unit_Ability>
      <Unit_Ability><Type>ENERGY_WEAPON</Type><SFXEvent_Special_Ability_Loop> Beam_Loop </SFXEvent_Special_Ability_Loop></Unit_Ability>
    </Unit_Abilities_Data>
    <Abilities SubObjectList="Yes">
      <Stealth_Ability Name="Hide"><SFXEvent_Activate> Unit_Stealth_On </SFXEvent_Activate></Stealth_Ability>
      <Force_Healing_Ability Name="Heal2"><SFXEvent_Activate>Heal_On</SFXEvent_Activate></Force_Healing_Ability>
      <Laser_Defense_Ability Name="Zapper"><Zap_SFXEvent> Laser_Zap </Zap_SFXEvent></Laser_Defense_Ability>
      <Tactical_Bribe_Ability Name="Bribe"><SFXEvent_Success>Bribe_Yes</SFXEvent_Success><SFXEvent_Fail>Bribe_No</SFXEvent_Fail>
      </Tactical_Bribe_Ability>
      <Summon_Ability Name="Sum"><SFXEvent_Unit_Summoned>Summon_Line</SFXEvent_Unit_Summoned></Summon_Ability>
      <Radioactive_Contaminate_Ability Name="Rad"><SFXEvent_Radioactive>Rad_Line</SFXEvent_Radioactive></Radioactive_Contaminate_Ability>
      <Buzz_Droids_Ability Name="Buzz"><Activate_SFX>Buzz_Line</Activate_SFX></Buzz_Droids_Ability>
    </Abilities>
  </SpaceUnit>
  <SpaceUnit Name="Healer">
    <Abilities SubObjectList="Yes">
      <Force_Healing_Ability Name="Heal"><SFXEvent_Activate>Heal_On</SFXEvent_Activate>
        <SFXEvent_Target_Affected>Heal_Target</SFXEvent_Target_Affected></Force_Healing_Ability>
    </Abilities>
  </SpaceUnit>
</SpaceUnits>
)");
    objects::ObjectTypeDatabase raw;
    CHECK(raw.load(files));
    objects::GameObjectTypes types;
    CHECK(types.build(raw));
    const objects::TypeIndex hero = types.resolve_name("Hero_Ship"), healer = types.resolve_name("Healer");
    CHECK(hero != objects::kNone && healer != objects::kNone);
    if (hero == objects::kNone || healer == objects::kNone) return;
    namespace ta = tactical::abilities;
    H::AbilitySfx s = H::ability_sfx(types, hero, ta::kStealth);  // forwarded by name
    CHECK(s.activate == "Unit_Stealth_On" && s.target_affected.empty());
    CHECK(H::ability_sfx(types, hero, ta::kSprint).activate.empty());  // a record without a special: nothing
    CHECK(H::ability_sfx(types, hero, 0).activate.empty());            // two entries, no record: no guess
    s = H::ability_sfx(types, healer, ta::kAreaEffectHeal);            // [port] the only entry
    CHECK(s.activate == "Heal_On" && s.target_affected == "Heal_Target");
    CHECK(H::ability_sfx(types, objects::kNone, ta::kStealth).activate.empty());

    // mcx4 t13 (legacy docs/re/MCX4_T13_RE_ANSWERS.md "Unit_Ability.SFXEvent_GUI_Unit_Ability_Activated family" and
    // "Unit_Ability.SFXEvent_Target_Ability"): the record of the clicked ability (#0 -> record 0, else record 1).
    CHECK(H::unit_ability_line(types, hero, ta::kStealth, "SFXEvent_GUI_Unit_Ability_Activated") == "Gui_Stealth_On");
    CHECK(H::unit_ability_line(types, hero, ta::kSprint, "SFXEvent_GUI_Unit_Ability_Deactivated") == "Gui_Sprint_Off");
    CHECK(H::unit_ability_line(types, hero, ta::kSprint, "SFXEvent_Target_Ability") == "Sprint_Target");
    CHECK(H::unit_ability_line(types, healer, ta::kStealth, "SFXEvent_GUI_Unit_Ability_Activated").empty());
    CHECK(H::plays_target_ability_line(19) && H::plays_target_ability_line(39) && !H::plays_target_ability_line(ta::kStealth));
    // mcx4 t13 (legacy docs/re/MCX4_PRES_RE_ANSWERS_2.md Q3): SFXEvent_Special_Ability_Loop from the ability's own record (the
    // third record here; the #0 / #1 pick would read the SPRINT record), on the six loop abilities, on the target for 31 / 55.
    // ledger: verifies Unit_Ability.SFXEvent_Special_Ability_Loop
    CHECK(H::ability_record_line(types, hero, ta::kEnergyWeapon, "SFXEvent_Special_Ability_Loop") == "Beam_Loop");
    CHECK(H::unit_ability_line(types, hero, ta::kEnergyWeapon, "SFXEvent_Special_Ability_Loop").empty());
    CHECK(H::ability_record_line(types, healer, ta::kEnergyWeapon, "SFXEvent_Special_Ability_Loop").empty());
    CHECK(H::special_ability_loop_user(ta::kTractorBeam) && H::special_ability_loop_user(ta::kLeechShields) &&
          H::special_ability_loop_user(ta::kForceSight) && !H::special_ability_loop_user(ta::kStealth));
    CHECK(H::special_ability_loop_on_target(ta::kEnergyWeapon) && H::special_ability_loop_on_target(ta::kSuperLaser) &&
          !H::special_ability_loop_on_target(ta::kTractorBeam));
    // "Laser_Defense_Ability.Zap_SFXEvent" / "Tactical_Bribe_Ability.SFXEvent_Success family": the ability element's field.
    CHECK(H::special_line(types, hero, "Laser_Defense_Ability", "Zap_SFXEvent") == "Laser_Zap");
    CHECK(H::special_line(types, hero, "Tactical_Bribe_Ability", "SFXEvent_Success") == "Bribe_Yes");
    CHECK(H::special_line(types, hero, "Tactical_Bribe_Ability", "SFXEvent_Fail") == "Bribe_No");
    CHECK(H::special_line(types, healer, "Laser_Defense_Ability", "Zap_SFXEvent").empty());
    // Summon_Ability SFXEvent_Unit_Summoned (filed as Tractor_Beam_Attack_Ability), Radioactive_Contaminate_Ability
    // SFXEvent_Radioactive, Buzz_Droids_Ability Activate_SFX (local owner only): the spawned object's line.
    {
        const H::SpawnLine sum = H::spawn_line(ta::kSummon), rad = H::spawn_line(ta::kRadioactiveContaminate),
                           buzz = H::spawn_line(ta::kBuzzDroids);
        CHECK(H::special_line(types, hero, sum.element, sum.field) == "Summon_Line" && !sum.local_only);
        CHECK(H::special_line(types, hero, rad.element, rad.field) == "Rad_Line" && !rad.local_only);
        CHECK(H::special_line(types, hero, buzz.element, buzz.field) == "Buzz_Line" && buzz.local_only);
        CHECK(H::spawn_line(ta::kStealth).element.empty());
    }
    // "Faction.SFXEvent_GUI_Toggle_Non_Hero_Ability_On family": own / ally issuer -> the plain line, hostile -> _Enemy_.
    CHECK(H::toggle_line_tag(true, false) == "SFXEvent_GUI_Toggle_Non_Hero_Ability_On");
    CHECK(H::toggle_line_tag(false, false) == "SFXEvent_GUI_Toggle_Non_Hero_Ability_Off");
    CHECK(H::toggle_line_tag(true, true) == "SFXEvent_GUI_Enemy_Toggle_Non_Hero_Ability_On");
    CHECK(H::toggle_line_tag(false, true) == "SFXEvent_GUI_Enemy_Toggle_Non_Hero_Ability_Off");

    CHECK(H::departure_sfx("Type_Jump", "Faction_Exit") == "Type_Jump");
    CHECK(H::departure_sfx("", "Faction_Exit") == "Faction_Exit");
    CHECK(H::shield_transition(-1, 0.0f) == 0 && H::shield_transition(-1, 1.0f) == 0);
    CHECK(H::shield_transition(1, 0.0f) == 1 && H::shield_transition(1, 0.5f) == 0);
    CHECK(H::shield_transition(0, 0.01f) == 2 && H::shield_transition(0, 0.0f) == 0);

    GameAudioHost off;  // never initialised: no-op
    off.ability_effect(types, healer, ta::kAreaEffectHeal, 5, Vec3{}, 6, Vec3{});
    CHECK(off.ability_sfx_played() == 0);
#ifdef ALAMO_GAME_HAVE_AUDIO
    GameData data;
    auto tree = make_tree(true);
    tree->add("Data\\XML\\SFXEventsTest.xml", R"(<?xml version="1.0"?>
<SFXEvents>
  <SFXEvent Name="Heal_On"><Is_3D>Yes</Is_3D><Samples>HEAL.WAV</Samples></SFXEvent>
  <SFXEvent Name="Heal_Target"><Is_3D>Yes</Is_3D><Samples>HEALT.WAV</Samples></SFXEvent>
</SFXEvents>
)");
    data.files = std::move(tree);
    GameAudioHost h;
    CHECK(h.init(data, AudioBackendKind::Null, 3));
    if (!h.active()) return;
    h.set_listener(Vec3{0, 0, 100}, Vec3{0, 0, 0});
    h.ability_effect(types, healer, ta::kAreaEffectHeal, 5, Vec3{10, 0, 0}, 6, Vec3{20, 0, 0});
    CHECK(h.ability_sfx_played() == 2);
    bool user = false, target = false;
    for (const audio::LogEntry& e : h.system()->log()) {
        if (e.op != audio::LogOp::SfxStart) continue;
        user = user || (e.event == "Heal_On" && e.owner == 5);
        target = target || (e.event == "Heal_Target" && e.owner == 6);
    }
    CHECK(user && target);
    h.ability_effect(types, healer, ta::kAreaEffectHeal, 5, Vec3{10, 0, 0}, 0, Vec3{});  // no target: the activation only
    CHECK(h.ability_sfx_played() == 3);

    // object_events reads AbilitySystem::activation_events (the EffectOn copy kept after the battle's tick clears
    // events(); the battle side: alamo_sim_battle_tests fog_effect_kept) once each, by serial: an entry whose user is
    // gone plays nothing, a smaller newest serial (a new battle's system) starts over.
    objects::ObjectStore store(objects::ModeKind::Space, objects::StoreKind::Primary, types);
    objects::SpawnParams sp;
    sp.type = healer;
    sp.owner = 0;
    sp.position = Vec3{10, 0, 0};
    const objects::ObjectId healer_id = store.id(store.create(sp));
    sp.type = hero;
    sp.position = Vec3{20, 0, 0};
    const objects::ObjectId victim = store.id(store.create(sp));
    std::vector<ta::AbilityActivation> log = {{30, ta::kAreaEffectHeal, static_cast<ta::ObjectId>(healer_id), static_cast<ta::ObjectId>(victim), 1},
                                              {30, ta::kAreaEffectHeal, 999, ta::kNoObject, 2}};
    h.update(5.0);
    h.ability_activations(log, store, types);
    CHECK(h.ability_sfx_played() == 5);  // activation + target; the unknown user: nothing
    h.ability_activations(log, store, types);
    CHECK(h.ability_sfx_played() == 5);  // already played
    log.push_back({31, ta::kAreaEffectHeal, static_cast<ta::ObjectId>(healer_id), ta::kNoObject, 3});
    h.update(5.0);
    h.ability_activations(log, store, types);
    CHECK(h.ability_sfx_played() == 6);  // only the new one
    log = {{1, ta::kAreaEffectHeal, static_cast<ta::ObjectId>(healer_id), ta::kNoObject, 1}};
    h.update(5.0);
    h.ability_activations(log, store, types);
    CHECK(h.ability_sfx_played() == 7);  // a new system's serial 1
#endif
}

// aud2 (both builds): the production tag names; with engine/audio, every aud2 hookup on a synthetic tree and the null
// backend - t01 planet reveal plays the Audio.xml constant's value, t02 Master_Volume_Restore returns to the profile's
// master, t03 production begin / cancel / complete (+ Build_Music_Completed), t04 no updates and paused music while the
// application is inactive, t07 the fleet departure line, t08 the strategic win / lose music.
// mcx4 t13 batch Q (legacy docs/re/MCX4_T13_RE_ANSWERS.md "GameObjectType.Spin_Away_On_Death_SFXEvent_Start_Die" and
// "GameObjectType.Selt_Destruct_SFXEvent_Start_Die"): a spin-away corpse plays its type's line once; a self-destruct
// countdown starts its line once and stops it when the unit leaves the counting list.
// ledger: verifies GameObjectType.Spin_Away_On_Death_SFXEvent_Start_Die
// ledger: verifies GameObjectType.Selt_Destruct_SFXEvent_Start_Die
void test_spin_and_self_destruct() {
    MemoryProvider files;
    files.add("Data\\XML\\GameObjectFiles.xml", "<L><File>Q.xml</File></L>");
    files.add("Data\\XML\\Q.xml", R"(<?xml version="1.0"?>
<SpaceUnits>
  <SpaceUnit Name="Spinner"><Spin_Away_On_Death_SFXEvent_Start_Die>Spin_Die</Spin_Away_On_Death_SFXEvent_Start_Die>
    <Selt_Destruct_SFXEvent_Start_Die>SD_Count</Selt_Destruct_SFXEvent_Start_Die></SpaceUnit>
  <SpaceUnit Name="Plain"></SpaceUnit>
</SpaceUnits>
)");
    objects::ObjectTypeDatabase raw;
    CHECK(raw.load(files));
    objects::GameObjectTypes types;
    CHECK(types.build(raw));
    const objects::TypeIndex spinner = types.resolve_name("Spinner"), plain = types.resolve_name("Plain");
    CHECK(spinner != objects::kNone && plain != objects::kNone);
    CHECK(types.get_string(spinner, "Spin_Away_On_Death_SFXEvent_Start_Die") == "Spin_Die");
    CHECK(types.get_string(spinner, "Selt_Destruct_SFXEvent_Start_Die") == "SD_Count");
    GameAudioHost off;  // inactive host: nothing
    const std::vector<GameAudioHost::SpinDeath> spins = {{7, spinner, Vec3{1, 0, 0}}, {8, plain, Vec3{}}};
    CHECK(off.spin_away_deaths(spins, types) == 0);
#ifdef ALAMO_GAME_HAVE_AUDIO
    GameData data;
    auto tree = make_tree(true);
    tree->add("Data\\XML\\SFXEventsTest.xml", R"(<?xml version="1.0"?>
<SFXEvents>
  <SFXEvent Name="Spin_Die"><Is_3D>Yes</Is_3D><Samples>SPIN.WAV</Samples></SFXEvent>
  <SFXEvent Name="SD_Count"><Is_3D>Yes</Is_3D><Samples>SD.WAV</Samples><Play_Count>-1</Play_Count></SFXEvent>
</SFXEvents>
)");
    data.files = std::move(tree);
    GameAudioHost h;
    CHECK(h.init(data, AudioBackendKind::Null, 3));
    if (!h.active()) return;
    h.set_listener(Vec3{0, 0, 100}, Vec3{0, 0, 0});
    CHECK(h.spin_away_deaths(spins, types) == 1);  // the plain type has no line
    CHECK(h.spin_away_deaths(spins, types) == 0);  // once per death
    CHECK(starts(h, "Spin_Die") == 1);
    const std::vector<GameAudioHost::SelfDestructCue> counting = {{9, "SD_Count", Vec3{2, 0, 0}}};
    CHECK(h.self_destruct_lines(counting) == 1 && h.self_destruct_lines(counting) == 0);  // started once
    CHECK(starts(h, "SD_Count") == 1);
    h.self_destruct_lines({});  // cancelled / blown up: stopped
    int stops = 0;
    for (const audio::LogEntry& e : h.system()->log()) stops += e.op == audio::LogOp::SfxStop && e.event == "SD_Count" ? 1 : 0;
    CHECK(stops == 1);
#endif
}

// #61 (DeathBehavior effects 0x58cf90 phase 2, run from its detach hook 0x58d830 when the object is deleted): a unit's
// death plays its Death_SFXEvent_Start_Die, and the removal of its death clone plays the clone type's
// Death_SFXEvent_End_Die afterwards - once per clone: a persisting clone when its countdown ends, a clone removed at
// once, a clone flushed between two ticks; nothing for the clones the battle's end drops.
// ledger: verifies GameObjectType.Death_SFXEvent_End_Die
void test_death_end_sfx() {
    MemoryProvider files;
    files.add("Data\\XML\\GameObjectFiles.xml", "<L><File>D.xml</File></L>");
    files.add("Data\\XML\\D.xml", R"(<?xml version="1.0"?>
<GroundVehicles>
  <GroundVehicle Name="Walker"><Death_SFXEvent_Start_Die>Walker_Start</Death_SFXEvent_Start_Die></GroundVehicle>
  <GroundVehicle Name="Walker_Clone"><Death_SFXEvent_End_Die>Walker_End</Death_SFXEvent_End_Die></GroundVehicle>
</GroundVehicles>
)");
    objects::ObjectTypeDatabase raw;
    CHECK(raw.load(files));
    objects::GameObjectTypes types;
    CHECK(types.build(raw));
    const objects::TypeIndex walker = types.resolve_name("Walker"), clone = types.resolve_name("Walker_Clone");
    CHECK(walker != objects::kNone && clone != objects::kNone);
    CHECK(types.get_string(clone, "Death_SFXEvent_End_Die") == "Walker_End");
    objects::ObjectStore store(objects::ModeKind::Land, objects::StoreKind::Primary, types);
    auto spawn = [&store](objects::TypeIndex t, Vec3 at, bool death_clone) {
        objects::SpawnParams sp;
        sp.type = t;
        sp.owner = 0;
        sp.position = at;
        const objects::ObjectHandle h = store.create(sp);
        if (death_clone) store.set_flag(h, objects::kFlagDeathClone);
        return h;
    };
    const objects::ObjectHandle unit = spawn(walker, Vec3{5, 0, 0}, false);
    const objects::ObjectHandle persisting = spawn(clone, Vec3{5, 0, 0}, true);
    GameAudioHost off;  // inactive host: nothing
    CHECK(off.unit_died(types, store.id(unit), walker, Vec3{5, 0, 0}, true) == 0);
    CHECK(off.death_clone_ends(store, types, false) == 0);
#ifdef ALAMO_GAME_HAVE_AUDIO
    GameData data;
    auto tree = make_tree(true);
    tree->add("Data\\XML\\SFXEventsTest.xml", R"(<?xml version="1.0"?>
<SFXEvents>
  <SFXEvent Name="Walker_Start"><Is_3D>Yes</Is_3D><Samples>WALKER_START.WAV</Samples></SFXEvent>
  <SFXEvent Name="Walker_End"><Is_3D>Yes</Is_3D><Samples>WALKER_END.WAV</Samples></SFXEvent>
</SFXEvents>
)");
    data.files = std::move(tree);
    GameAudioHost h;
    CHECK(h.init(data, AudioBackendKind::Null, 3));
    if (!h.active()) return;
    h.set_listener(Vec3{0, 0, 100}, Vec3{0, 0, 0});
    // the tick of the death: the unit's start-die line; its clone runs its death sequence (no end yet)
    CHECK(h.unit_died(types, store.id(unit), walker, Vec3{5, 0, 0}, false) != 0);
    store.destroy(unit);
    CHECK(h.death_clone_ends(store, types, false) == 0);
    CHECK(starts(h, "Walker_Start") == 1 && starts(h, "Walker_End") == 0);
    h.update(1.0);
    // the countdown ends: the clone is removed (dead until the next flush) - its end-die line, once
    store.destroy(persisting);
    CHECK(h.death_clone_ends(store, types, false) == 1);
    CHECK(h.death_clone_ends(store, types, false) == 0);
    store.flush();
    CHECK(h.death_clone_ends(store, types, false) == 0);
    CHECK(starts(h, "Walker_End") == 1 && h.stats().death_ends == 1);
    int start_at = -1, end_at = -1, i = 0;
    for (const audio::LogEntry& e : h.system()->log()) {
        if (e.op == audio::LogOp::SfxStart && e.event == "Walker_Start" && start_at < 0) start_at = i;
        if (e.op == audio::LogOp::SfxStart && e.event == "Walker_End" && end_at < 0) end_at = i;
        ++i;
    }
    CHECK(start_at >= 0 && end_at > start_at);  // the end of the death after its start
    // a clone removed at once (Remove_Upon_Death, no clip): seen dead on its first tick, still voiced
    store.destroy(spawn(clone, Vec3{7, 0, 0}, true));
    CHECK(h.death_clone_ends(store, types, false) == 1);
    // a clone gone between two ticks (removed and flushed): voiced from its last known state
    const objects::ObjectHandle quick = spawn(clone, Vec3{9, 0, 0}, true);
    CHECK(h.death_clone_ends(store, types, false) == 0);
    store.destroy(quick);
    store.flush();
    CHECK(h.death_clone_ends(store, types, false) == 1);
    // a clone without the tag: nothing; the battle's end drops the clones left: nothing
    store.destroy(spawn(walker, Vec3{}, true));
    CHECK(h.death_clone_ends(store, types, false) == 0);
    store.flush();
    const objects::ObjectHandle left = spawn(clone, Vec3{}, true);
    CHECK(h.death_clone_ends(store, types, false) == 0);
    store.destroy(left);
    CHECK(h.death_clone_ends(store, types, true) == 0);
    CHECK(starts(h, "Walker_End") == 3 && h.stats().death_ends == 3);
#endif
}

void test_aud2() {
    using H = GameAudioHost;
    CHECK(H::production_tag(H::Production::Begin) == "SFXEvent_Build_Started");
    CHECK(H::production_tag(H::Production::Cancel) == "SFXEvent_Build_Cancelled");
    CHECK(H::production_tag(H::Production::Complete) == "SFXEvent_Build_Complete");
    // mcx4 t13 (legacy docs/re/MCX4_T13_RE_ANSWERS.md "GameObjectType.SFXEvent_Tactical_Build_Started family"): the speech first,
    // then the tactical line in a battle, then the plain one.
    // ledger: verifies GameObjectType.SFXEvent_Tactical_Build_Started
    // ledger: verifies GameObjectType.SFXEvent_Tactical_Build_Complete
    // ledger: verifies GameObjectType.SFXEvent_Tactical_Build_Cancelled
    // ledger: verifies GameObjectType.Build_Speech_Underway
    // ledger: verifies GameObjectType.Build_Speech_Completed
    // ledger: verifies GameObjectType.Build_Speech_Stopped
    {
        using A = std::array<std::string_view, 3>;
        CHECK((H::production_tags(H::Production::Begin, true) == A{"Build_Speech_Underway", "SFXEvent_Tactical_Build_Started", "SFXEvent_Build_Started"}));
        CHECK((H::production_tags(H::Production::Complete, true) == A{"Build_Speech_Completed", "SFXEvent_Tactical_Build_Complete", "SFXEvent_Build_Complete"}));
        CHECK((H::production_tags(H::Production::Cancel, true) == A{"Build_Speech_Stopped", "SFXEvent_Tactical_Build_Cancelled", "SFXEvent_Build_Cancelled"}));
        CHECK((H::production_tags(H::Production::Begin, false) == A{"Build_Speech_Underway", "", "SFXEvent_Build_Started"}));
    }
    MemoryProvider files;
    files.add("Data\\XML\\GameObjectFiles.xml", "<L><File>Aud2.xml</File></L>");
    files.add("Data\\XML\\Aud2.xml", R"(<?xml version="1.0"?>
<SpaceUnits>
  <SpaceUnit Name="Built_Ship">
    <SFXEvent_Build_Started> HD_Build </SFXEvent_Build_Started>
    <SFXEvent_Build_Cancelled> HD_Cancel </SFXEvent_Build_Cancelled>
    <SFXEvent_Build_Complete> HD_Complete </SFXEvent_Build_Complete>
  </SpaceUnit>
  <SpaceUnit Name="Death_Star">
    <SFXEvent_Build_Complete> HD_Complete </SFXEvent_Build_Complete>
    <Build_Music_Completed> DS_Done </Build_Music_Completed>
  </SpaceUnit>
  <SpaceUnit Name="Silent_Ship"></SpaceUnit>
</SpaceUnits>
)");
    objects::ObjectTypeDatabase raw;
    CHECK(raw.load(files));
    objects::GameObjectTypes types;
    CHECK(types.build(raw));
    const objects::TypeIndex ship = types.resolve_name("Built_Ship"), ds = types.resolve_name("Death_Star"),
                             silent = types.resolve_name("Silent_Ship");
    CHECK(ship != objects::kNone && ds != objects::kNone && silent != objects::kNone);
    GameAudioHost off;  // never initialised: every call is a no-op, the activation bookkeeping still runs
    CHECK(off.production(types, ship, H::Production::Begin) == 0 && off.fleet_departure(true, true) == 0);
    off.planet_revealed();
    off.campaign_end(true);
    off.set_app_active(false);
    CHECK(!off.app_active() && off.stats().deactivations == 1);
#ifdef ALAMO_GAME_HAVE_AUDIO
    if (ship == objects::kNone || ds == objects::kNone || silent == objects::kNone) return;
    auto tree = std::make_unique<MemoryProvider>();
    tree->add("Data\\XML\\SFXEventFiles.xml", "<SFXEvent_Files><File>SFXEventsAud2.xml</File></SFXEvent_Files>");
    tree->add("Data\\XML\\SFXEventsAud2.xml", R"(<?xml version="1.0"?>
<SFXEvents>
  <SFXEvent Name="GUI_Planet_Mouse_Over"><Is_2D>Yes</Is_2D><Is_3D>No</Is_3D><Is_GUI>Yes</Is_GUI><Samples>MO.WAV</Samples></SFXEvent>
  <SFXEvent Name="HD_Build"><Is_2D>Yes</Is_2D><Is_3D>No</Is_3D><Samples>B.WAV</Samples></SFXEvent>
  <SFXEvent Name="HD_Cancel"><Is_2D>Yes</Is_2D><Is_3D>No</Is_3D><Samples>C.WAV</Samples></SFXEvent>
  <SFXEvent Name="HD_Complete"><Is_2D>Yes</Is_2D><Is_3D>No</Is_3D><Samples>D.WAV</Samples></SFXEvent>
  <SFXEvent Name="Galactic_Fleet_Hyperspace"><Is_2D>Yes</Is_2D><Is_3D>No</Is_3D><Is_GUI>Yes</Is_GUI><Samples>HS.WAV</Samples></SFXEvent>
</SFXEvents>
)");
    tree->add("Data\\XML\\MusicEvents.xml", R"(<?xml version="1.0"?>
<MusicEvents>
  <MusicEvent Name="Gal_Rebel"><Files>G1.MP3</Files><Loop>Yes</Loop></MusicEvent>
  <MusicEvent Name="DS_Done"><Files>DS.MP3</Files></MusicEvent>
  <MusicEvent Name="Strat_Win"><Files>W.MP3</Files></MusicEvent>
  <MusicEvent Name="Strat_Lose"><Files>L.MP3</Files></MusicEvent>
  <MusicEvent Name="Strat_Lose_Empire"><Files>LE.MP3</Files></MusicEvent>
  <MusicEvent Name="Sum_Win"><Files>SW.MP3</Files></MusicEvent>
  <MusicEvent Name="Sum_Lose"><Files>SL.MP3</Files></MusicEvent>
</MusicEvents>
)");
    tree->add("Data\\XML\\SpeechEvents.xml", "<SpeechEvents/>");
    tree->add("Data\\XML\\Audio.xml", "<Audio><SFXEvent_Planet_Revealed> GUI_Planet_Mouse_Over </SFXEvent_Planet_Revealed>"
              "<Music_Event_Battle_End_Summary_Screen_Win> Sum_Win </Music_Event_Battle_End_Summary_Screen_Win>"
              "<Music_Event_Battle_End_Summary_Screen_Lose> Sum_Lose </Music_Event_Battle_End_Summary_Screen_Lose></Audio>");
    tree->add("Data\\XML\\FactionFiles.xml", "<Faction_Files><File>Factions.xml</File></Faction_Files>");
    tree->add("Data\\XML\\Factions.xml", R"(<?xml version="1.0" ?>
<Factions>
  <Faction Name="Rebel"><Strategic_Map_Music_Event> Gal_Rebel </Strategic_Map_Music_Event>
    <Music_Event_Strategic_Win> Strat_Win </Music_Event_Strategic_Win>
    <Music_Event_Strategic_Lose> Strat_Lose </Music_Event_Strategic_Lose>
    <Music_Event_Strategic_Lose_Vs_Faction> Empire, Strat_Lose_Empire </Music_Event_Strategic_Lose_Vs_Faction></Faction>
</Factions>
)");
    GameData data;
    data.files = std::move(tree);
    GameAudioHost h;
    CHECK(h.init(data, AudioBackendKind::Null, 5));
    if (!h.active()) return;
    h.music(MusicScene::Galactic, "Rebel");

    // t01: the constant's value plays, not the tag name (which is no SFX event).
    h.planet_revealed();
    CHECK(starts(h, "GUI_Planet_Mouse_Over") == 1 && h.stats().planet_reveals == 1);
    CHECK(starts(h, "SFXEvent_Planet_Revealed") == 0);

    // #223 (retail answer on #205): volume changes (profile load, options apply / preview) save no master level, so
    // Lua Master_Volume_Restore after an options preview leaves the previewed master.
    h.set_volumes(0.55f, 0.8f, 0.8f, 0.8f);  // the profile
    h.set_volumes(0.3f, 0.8f, 0.8f, 0.8f);   // an options preview
    CHECK(!h.system()->saved_master_volume());
    h.system()->master_volume_restore();
    CHECK(h.system()->bus_level(audio::AudioBus::Master) == 0.3f);

    // t03: begin / cancel / complete play the type's tags; a completion with Build_Music_Completed plays it in mode 4.
    CHECK(h.production(types, ship, H::Production::Begin) != 0 && starts(h, "HD_Build") == 1);
    CHECK(h.production(types, ship, H::Production::Cancel) != 0 && starts(h, "HD_Cancel") == 1);
    CHECK(h.production(types, ship, H::Production::Complete) != 0 && starts(h, "HD_Complete") == 1);
    CHECK(h.stats().production_sfx == 3 && h.stats().build_music == 0 && cur(h) == "Gal_Rebel");
    CHECK(h.production(types, silent, H::Production::Begin) == 0);  // no tag: nothing
    h.update(5.0);  // the HUD lines end (Max_Instances) before the next completion line
    CHECK(h.production(types, ds, H::Production::Complete) != 0 && starts(h, "HD_Complete") == 2);
    CHECK(h.stats().build_music == 1 && cur(h) == "DS_Done" && mode(h) == audio::music_mode::kBuildComplete);

    // t07: owned or seen -> the hyperspace line; neither -> nothing.
    CHECK(h.fleet_departure(false, false) == 0 && starts(h, H::kFleetHyperspaceEvent) == 0);
    CHECK(h.fleet_departure(true, false) != 0 && starts(h, H::kFleetHyperspaceEvent) == 1);
    h.update(5.0);
    CHECK(h.fleet_departure(false, true) != 0 && starts(h, H::kFleetHyperspaceEvent) == 2 && h.stats().fleet_jumps == 2);

    // t04: inactive -> no updates (the music voice is held, nothing advances) and the music manager paused; active again
    // -> both resume. Only changes count.
    h.music(MusicScene::None);
    h.music(MusicScene::Galactic, "Rebel");
    h.update(0.0);
    CHECK(cur(h) == "Gal_Rebel" && !h.system()->music_paused());
    h.set_app_active(false);
    h.set_app_active(false);
    CHECK(!h.app_active() && h.system()->music_paused() && h.stats().deactivations == 1);
    const size_t log_inactive = h.system()->log().size();
    h.update(1.0);
    h.update(1.0);
    CHECK(h.system()->log().size() == log_inactive);  // no update ran: no voice ended, no music step
    // #227 (retail answer on #205): the SFX and speech managers are paused too, so a GUI SFX raised while inactive is
    // queued (the GUI pump runs no update) and gets its voice with the first update after reactivation.
    CHECK(h.system()->sfx_speech_paused());
    auto voices = [&h](std::string_view ev) {
        int n = 0;
        for (const audio::LogEntry& e : h.system()->log()) n += e.op == audio::LogOp::SfxVoice && e.event == ev ? 1 : 0;
        return n;
    };
    const int mo_starts = starts(h, "GUI_Planet_Mouse_Over"), mo_voices = voices("GUI_Planet_Mouse_Over");
    h.ui("GUI_Planet_Mouse_Over");
    CHECK(starts(h, "GUI_Planet_Mouse_Over") == mo_starts + 1 && voices("GUI_Planet_Mouse_Over") == mo_voices);
    h.set_app_active(true);
    CHECK(h.app_active() && !h.system()->music_paused() && !h.system()->sfx_speech_paused());
    h.update(0.0);
    CHECK(cur(h) == "Gal_Rebel" && voices("GUI_Planet_Mouse_Over") == mo_voices + 1);

    // t08 / mcx4 t13 (legacy docs/re/MCX4_T13_RE_ANSWERS.md "Faction.Music_Event_Strategic_Win family"): campaign won / lost ->
    // Audio.xml Music_Event_Battle_End_Summary_Screen_Win / _Lose in mode 16; the faction's Music_Event_Strategic_* rows
    // (retail-unused) are not played.
    h.campaign_end(true);
    CHECK(cur(h) == "Sum_Win" && mode(h) == audio::music_mode::kBattleEnd);
    h.campaign_end(false, "Empire");
    CHECK(cur(h) == "Sum_Lose" && mode(h) == audio::music_mode::kBattleEnd && h.stats().campaign_end_music == 2);
#endif
}

// aud8 t05: object positions are fed only during battles. Leaving the battle scene drops them, so a Lua Play_SFX_Event in
// galactic mode is not placed at the position its object id had in the battle.
void test_galactic_lua_sfx_position() {
#if defined(ALAMO_GAME_HAVE_AUDIO) && defined(ALAMO_AUDIO_HAVE_LUA)
    auto tree = make_tree(true);
    tree->add("Data\\XML\\SFXEventsTest.xml", R"(<?xml version="1.0"?>
<SFXEvents>
  <SFXEvent Name="Lua_Boom"><Is_2D>No</Is_2D><Is_3D>Yes</Is_3D><Samples>BOOM.WAV</Samples></SFXEvent>
</SFXEvents>
)");
    tree->add("Data\\Audio\\SFX\\BOOM.WAV", "RIFF");  // the null backend only asks whether the sample exists
    GameData data;
    data.files = std::move(tree);
    GameAudioHost h;
    CHECK(h.init(data, AudioBackendKind::Null, 13));
    if (!h.active()) return;
    uint32_t seq = 0;
    const auto play = [&h, &seq](int32_t object) {
        alamo::lua::PresentationEvent e;
        e.seq = seq;
        e.op = alamo::lua::PresentationOp::ObjectPlaySfxEvent;
        alamo::lua::PresentationArg ev, obj;
        ev.kind = alamo::lua::PresentationArg::Kind::String;
        ev.text = "Lua_Boom";
        obj.kind = alamo::lua::PresentationArg::Kind::Object;
        obj.id = object;
        e.args = {obj, ev};
        h.apply_presentation({e}, &seq);
        h.update(0.0);
        const audio::LogEntry* v = nullptr;
        for (const audio::LogEntry& l : h.system()->log())
            if (l.op == audio::LogOp::SfxVoice && l.event == "Lua_Boom") v = &l;
        return v ? v->gain : -1.0f;
    };
    // A space battle: object 7 is 20000 units from the listener, beyond the 3D max distance (15000): silent.
    h.music(MusicScene::Battle, "Rebel", true, "Space");
    h.set_listener(Vec3{0, -10, 0}, Vec3{0, 0, 0});
    h.set_object_positions({{7, Vec3{20000.0f, 0.0f, 0.0f}}});
    CHECK(play(7) == 0.0f);
    // Back on the galactic map, the same id: not at the battle's position.
    h.music(MusicScene::Galactic, "Rebel");
    h.set_listener(Vec3{0, -10, 0}, Vec3{0, 0, 0});
    CHECK(play(7) > 0.0f);  // before aud8 t05: 0, still 20000 units away
#endif
}

// #226 (retail answer on #205): the SFX manager's GUI-only flag (gate 4, with a modal GUI dialog open) is set while the
// attract demo runs (0x22f10 / 0x22bf0); a modal dialog alone (the in-game pause menu) refuses nothing.
void test_gui_only_attract() {
#ifdef ALAMO_GAME_HAVE_AUDIO
    auto tree = make_tree(true);
    tree->add("Data\\XML\\SFXEventsTest.xml", R"(<?xml version="1.0"?>
<SFXEvents>
  <SFXEvent Name="GUI_Click"><Is_2D>Yes</Is_2D><Is_3D>No</Is_3D><Is_GUI>Yes</Is_GUI><Max_Instances>9</Max_Instances><Samples>CLICK.WAV</Samples></SFXEvent>
  <SFXEvent Name="Hud_Beep"><Is_2D>Yes</Is_2D><Is_3D>No</Is_3D><Max_Instances>9</Max_Instances><Samples>BEEP.WAV</Samples></SFXEvent>
</SFXEvents>
)");
    GameData data;
    data.files = std::move(tree);
    GameAudioHost h;
    CHECK(h.init(data, AudioBackendKind::Null, 17));
    if (!h.active()) return;
    h.set_flags(true, false);  // a modal dialog over a paused session (the pause menu)
    CHECK(!h.gui_only());
    h.ui("Hud_Beep");
    CHECK(starts(h, "Hud_Beep") == 1);
    h.set_flags(true, true);  // the attract demo with a modal dialog: GUI events only
    CHECK(h.gui_only());
    h.ui("Hud_Beep");
    h.ui("GUI_Click");
    CHECK(starts(h, "Hud_Beep") == 1 && starts(h, "GUI_Click") == 1);
    h.set_flags(false, true);  // the attract demo without a modal dialog
    h.ui("Hud_Beep");
    CHECK(starts(h, "Hud_Beep") == 2);
    h.set_flags(true, false);  // the demo ended
    CHECK(!h.gui_only());
    h.ui("Hud_Beep");
    CHECK(starts(h, "Hud_Beep") == 3);
#endif
}

// #50 t02 (0x442f0 / 0x44590 / the resolvers [H]): the group line comes before the non-hero variant and falls back to the
// plain line when the type has none; the variant needs the first or second Unit_Abilities_Data entry active; a group
// counts objects that are not TEAM / FLEET / TRANSPORT.
void test_unit_responses_50() {
    using H = GameAudioHost;
    MemoryProvider files;
    files.add("Data\\XML\\GameObjectFiles.xml", "<L><File>T50R.xml</File></L>");
    files.add("Data\\XML\\T50R.xml", R"(<?xml version="1.0"?>
<SpaceUnits>
  <SpaceUnit Name="Broadside"><SFXEvent_Attack> Bs_Attack </SFXEvent_Attack><SFXEvent_Move> Bs_Move </SFXEvent_Move>
    <SFXEvent_Group_Attack> Bs_Group_Attack </SFXEvent_Group_Attack>
    <SFXEvent_Attack_With_Non_Hero_Ability> Bs_Barrage </SFXEvent_Attack_With_Non_Hero_Ability>
    <Unit_Abilities_Data SubObjectList="Yes">
      <Unit_Ability><Type>STEALTH</Type></Unit_Ability><Unit_Ability><Type>SPRINT</Type></Unit_Ability>
      <Unit_Ability><Type>ENERGY_WEAPON</Type></Unit_Ability>
    </Unit_Abilities_Data></SpaceUnit>
  <SpaceUnit Name="Plain"><SFXEvent_Move> Pl_Move </SFXEvent_Move></SpaceUnit>
  <SpaceUnit Name="Squad_Team"><Behavior>TEAM</Behavior></SpaceUnit>
</SpaceUnits>
)");
    objects::ObjectTypeDatabase raw;
    CHECK(raw.load(files));
    objects::GameObjectTypes types;
    CHECK(types.build(raw));
    const objects::TypeIndex bs = types.resolve_name("Broadside"), plain = types.resolve_name("Plain");
    CHECK(bs != objects::kNone && plain != objects::kNone);
    if (bs == objects::kNone || plain == objects::kNone) return;
    namespace ta = tactical::abilities;
    const auto only = [](uint8_t t) { return [t](uint8_t a) { return a == t; }; };
    CHECK(H::non_hero_variant_active(types, bs, only(ta::kStealth)) && H::non_hero_variant_active(types, bs, only(ta::kSprint)));
    CHECK(!H::non_hero_variant_active(types, bs, only(ta::kEnergyWeapon)));  // the third entry does not count
    CHECK(!H::non_hero_variant_active(types, bs, [](uint8_t) { return false; }) &&
          !H::non_hero_variant_active(types, plain, [](uint8_t) { return true; }));
    // group_count: plain units count; a TEAM container and an unknown id do not.
    objects::ObjectStore store(objects::ModeKind::Space, objects::StoreKind::Primary, types);
    objects::SpawnParams sp;
    sp.type = bs;
    sp.owner = 0;
    const uint32_t a = static_cast<uint32_t>(store.id(store.create(sp)));
    const uint32_t b = static_cast<uint32_t>(store.id(store.create(sp)));
    sp.type = types.resolve_name("Squad_Team");
    const uint32_t team = static_cast<uint32_t>(store.id(store.create(sp)));
    const std::vector<uint32_t> two = {a, b}, one = {a}, gone = {a, 0x7FFFFFF0u}, with_team = {a, team};
    CHECK(H::group_count(store, two) == 2 && H::group_count(store, one) == 1 && H::group_count(store, gone) == 1);
    CHECK(H::group_count(store, with_team) == 1);
#ifdef ALAMO_GAME_HAVE_AUDIO
    auto tree = make_tree(true);
    tree->add("Data\\XML\\SFXEventsTest.xml", R"(<?xml version="1.0"?>
<SFXEvents>
  <SFXEvent Name="Bs_Attack"><Is_2D>Yes</Is_2D><Is_3D>No</Is_3D><Max_Instances>8</Max_Instances><Samples>A.WAV</Samples></SFXEvent>
  <SFXEvent Name="Bs_Move"><Is_2D>Yes</Is_2D><Is_3D>No</Is_3D><Max_Instances>8</Max_Instances><Samples>M.WAV</Samples></SFXEvent>
  <SFXEvent Name="Bs_Group_Attack"><Is_2D>Yes</Is_2D><Is_3D>No</Is_3D><Max_Instances>8</Max_Instances><Samples>G.WAV</Samples></SFXEvent>
  <SFXEvent Name="Bs_Barrage"><Is_2D>Yes</Is_2D><Is_3D>No</Is_3D><Max_Instances>8</Max_Instances><Samples>B.WAV</Samples></SFXEvent>
  <SFXEvent Name="Pl_Move"><Is_2D>Yes</Is_2D><Is_3D>No</Is_3D><Max_Instances>8</Max_Instances><Samples>P.WAV</Samples></SFXEvent>
</SFXEvents>
)");
    GameData data;
    data.files = std::move(tree);
    H h;
    CHECK(h.init(data, AudioBackendKind::Null, 3));
    if (!h.active()) return;
    h.unit_response(types, bs, 5, UnitResponse::Attack, true, 0, 0, -1, true);  // group first (0x44590 step 2)
    CHECK(starts(h, "Bs_Group_Attack") == 1 && starts(h, "Bs_Barrage") == 0);
    h.unit_response(types, bs, 5, UnitResponse::Attack, false, 0, 0, -1, true);  // the resolver's variant
    CHECK(starts(h, "Bs_Barrage") == 1);
    h.unit_response(types, bs, 5, UnitResponse::Attack, false, 0, 0, -1, false);
    CHECK(starts(h, "Bs_Attack") == 1);
    h.unit_response(types, bs, 5, UnitResponse::Move, true, 0, 0, -1, false);  // no Group_Move: the plain line
    CHECK(starts(h, "Bs_Move") == 1);
    h.unit_response(types, plain, 6, UnitResponse::Move, false, 0, 0, -1, true);  // no variant set: the plain line
    CHECK(starts(h, "Pl_Move") == 1);
#endif
}

// ref2 t02 (alamo/sim/presentation_events.hpp): one tick's presentation events -> GameAudioHost::on_events. The local
// player's tactical production begin / cancel / complete play the built type's speech or SFXEvent_Tactical_Build_* line
// (MCX4_T13_RE_ANSWERS "GameObjectType.SFXEvent_Tactical_Build_Started family"), no build music in a battle; another
// player's events, a negative type and no local player play nothing.
void test_presentation_events_ref2() {
    using K = sim::PresentationEventKind;
    MemoryProvider files;
    files.add("Data\\XML\\GameObjectFiles.xml", "<L><File>Ref2.xml</File></L>");
    files.add("Data\\XML\\Ref2.xml", R"(<?xml version="1.0"?>
<SpaceUnits>
  <SpaceUnit Name="Built_Fighter">
    <SFXEvent_Tactical_Build_Started> TB_Start </SFXEvent_Tactical_Build_Started>
    <SFXEvent_Tactical_Build_Cancelled> TB_Cancel </SFXEvent_Tactical_Build_Cancelled>
    <SFXEvent_Tactical_Build_Complete> TB_Done </SFXEvent_Tactical_Build_Complete>
    <SFXEvent_Build_Complete> Gal_Done </SFXEvent_Build_Complete>
    <Build_Music_Completed> Built_Music </Build_Music_Completed>
  </SpaceUnit>
  <SpaceUnit Name="Spoken_Upgrade">
    <SFXEvent_Tactical_Build_Started> TB_Start </SFXEvent_Tactical_Build_Started>
    <Build_Speech_Underway> Speech_Underway </Build_Speech_Underway>
  </SpaceUnit>
</SpaceUnits>
)");
    objects::ObjectTypeDatabase raw;
    CHECK(raw.load(files));
    objects::GameObjectTypes types;
    CHECK(types.build(raw));
    const objects::TypeIndex fighter = types.resolve_name("Built_Fighter"), spoken = types.resolve_name("Spoken_Upgrade");
    CHECK(fighter != objects::kNone && spoken != objects::kNone);
    if (fighter == objects::kNone || spoken == objects::kNone) return;
    const auto ev = [](K kind, int32_t player, objects::TypeIndex type, uint32_t order) {
        sim::PresentationEvent e;
        e.kind = kind;
        e.player = player;
        e.type = static_cast<int32_t>(type);
        e.object = 7;
        e.order = order;
        return e;
    };
    GameAudioHost off;  // never initialised: a no-op
    off.on_events({ev(K::ProductionBegin, 0, fighter, 1)}, types, 0);
    CHECK(off.stats().production_sfx == 0);
#ifdef ALAMO_GAME_HAVE_AUDIO
    auto tree = std::make_unique<MemoryProvider>();
    tree->add("Data\\XML\\SFXEventFiles.xml", "<SFXEvent_Files><File>SFXEventsRef2.xml</File></SFXEvent_Files>");
    tree->add("Data\\XML\\SFXEventsRef2.xml", R"(<?xml version="1.0"?>
<SFXEvents>
  <SFXEvent Name="TB_Start"><Is_2D>Yes</Is_2D><Is_3D>No</Is_3D><Samples>S.WAV</Samples></SFXEvent>
  <SFXEvent Name="TB_Cancel"><Is_2D>Yes</Is_2D><Is_3D>No</Is_3D><Samples>C.WAV</Samples></SFXEvent>
  <SFXEvent Name="TB_Done"><Is_2D>Yes</Is_2D><Is_3D>No</Is_3D><Samples>D.WAV</Samples></SFXEvent>
  <SFXEvent Name="Gal_Done"><Is_2D>Yes</Is_2D><Is_3D>No</Is_3D><Samples>G.WAV</Samples></SFXEvent>
  <SFXEvent Name="HG_Enroute"><Is_2D>Yes</Is_2D><Is_3D>No</Is_3D><Max_Instances>4</Max_Instances><Samples>E.WAV</Samples></SFXEvent>
</SFXEvents>
)");
    tree->add("Data\\XML\\MusicEvents.xml", R"(<?xml version="1.0"?>
<MusicEvents><MusicEvent Name="Built_Music"><Files>BM.MP3</Files></MusicEvent></MusicEvents>
)");
    tree->add("Data\\XML\\SpeechEvents.xml", R"(<?xml version="1.0"?>
<SpeechEvents><SpeechEvent Name="Speech_Underway"><Files>SU.WAV</Files></SpeechEvent></SpeechEvents>
)");
    tree->add("Data\\XML\\Audio.xml", "<Audio/>");
    tree->add("Data\\XML\\FactionFiles.xml", "<Faction_Files><File>Factions.xml</File></Faction_Files>");
    tree->add("Data\\XML\\Factions.xml", "<Factions><Faction Name=\"Rebel\"><Reinforcements_Enroute_SFXEvent> HG_Enroute "
                                          "</Reinforcements_Enroute_SFXEvent></Faction></Factions>");
    GameData data;
    data.files = std::move(tree);
    GameAudioHost h;
    CHECK(h.init(data, AudioBackendKind::Null, 9));
    if (!h.active()) return;
    // One tick: the local player (1) queues, cancels and finishes fighters; player 0's order and a bad type are skipped.
    const sim::PresentationEvents tick = {ev(K::ProductionBegin, 1, fighter, 1), ev(K::ProductionBegin, 0, fighter, 2),
                                          ev(K::ProductionCanceled, 1, fighter, 1), ev(K::ProductionCompleted, 1, fighter, 3),
                                          ev(K::ProductionCompleted, 0, fighter, 2), ev(K::ProductionBegin, 1, objects::kNone, 4)};
    h.on_events(tick, types, 1);
    CHECK(starts(h, "TB_Start") == 1 && starts(h, "TB_Cancel") == 1 && starts(h, "TB_Done") == 1);
    CHECK(starts(h, "Gal_Done") == 0);                    // the tactical line, not the galactic one
    CHECK(h.stats().production_sfx == 3 && h.stats().build_music == 0);  // no build music in a battle
    CHECK(h.system()->current_music() != "Built_Music");
    // A set Build_Speech_Underway replaces the SFX line (queued speech).
    h.on_events({ev(K::ProductionBegin, 1, spoken, 5)}, types, 1);
    CHECK(starts(h, "TB_Start") == 1 && h.stats().production_sfx == 4);
    // No local player (attract demo): nothing.
    h.on_events(tick, types, -1);
    CHECK(h.stats().production_sfx == 4);
    // An empty tick: nothing.
    h.on_events({}, types, 1);
    CHECK(h.stats().production_sfx == 4);
    // #50 t06: the local player's single-ship hangar launch plays the faction's Reinforcements_Enroute_SFXEvent (2D);
    // another player's does not.
    h.music(MusicScene::Galactic, "Rebel");  // the local faction
    h.on_events({ev(K::HangarLaunch, 0, fighter, 0), ev(K::HangarLaunch, 1, fighter, 0)}, types, 1);
    CHECK(starts(h, "HG_Enroute") == 1 && h.stats().hangar_enroute_lines == 1 && h.stats().production_sfx == 4);
#endif
}

}  // namespace

int main() {
    test_delayed_fire_sounds();  // trl2 t08
    test_object_sfx_t05();
    test_unit_lost_sighting_50();  // #50 t01 / t03
    test_galactic_activate_t06();  // #62 t06
    test_galactic_lua_sfx_position();  // aud8 t05
    test_gui_only_attract();           // #226
    test_inactive();
    test_control_point_lines();
    test_garrison_and_clash_lines();
    test_health_and_hardpoint_lines();  // mcx4 t13
    test_control_lines();               // mcx4 t13 batch C
    test_battle_result_lines();         // mcx4 t13 batch D
    test_retreat_begin_lines();         // mcx4 t13 batch E
    test_special_weapon_lines();        // mcx4 t13 batch G
    test_move_into_field();             // mcx4 t13 batch G
    test_base_shield_lines();           // mcx4 t13 batch I
    test_unit_room();                   // mcx4 t13 batch J
    test_strike_available_tags();       // mcx4 t13 batch M
    test_land_damage_stage();           // mcx4 t13 batch O
    test_pad_lines();                   // mcx4 t13 batch P
    test_tsw_approach();                // mcx4 t13 batch S
    test_point_contested();             // mcx4 t13 batch U
    test_command_rank();                // mcx4 tac2 handover
    test_turret_turning();              // mcx4 t13
    test_retreat_refusal_tag();         // mcx4 t13
    test_intro_speech_t07();
    test_aud2();
    test_presentation_events_ref2();  // ref2 t02
    test_unit_responses_50();          // #50 t02
    test_spin_and_self_destruct();  // mcx4 t13 batch Q
    test_death_end_sfx();           // #61
#ifdef ALAMO_GAME_HAVE_AUDIO
    test_movie_music();
    test_structure_music();  // #62 t07
    test_missing_credits_event();
    test_super_weapon_music();
    test_object_mute();  // #225
    test_battle_hookups_aud1();
#endif
    std::printf(g_fail == 0 ? "game audio host tests passed\n" : "%d game audio host check(s) FAILED\n", g_fail);
    return g_fail == 0 ? 0 : 1;
}
