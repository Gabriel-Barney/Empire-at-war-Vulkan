// alamo_game host, what the smoke script (app_script.cpp, app_script_saveload.cpp) asks of the running game and does to
// it through one narrow interface instead of reaching into the App's members (#154). The App implements it
// (app_probe.cpp). The battle queries (in_battle ... battle_stats) are sim reads; the screen, dialog and HUD reads are
// the presentation state a player sees (the script clicks where the drawn HUD and dialogs are), never written into the
// sim except through the actions a player could take (HUD actions, menu actions, submit).
#pragma once
#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "alamo/commands/command.hpp"
#include "alamo/core/galaxy.hpp"
#include "alamo/objects/object_store.hpp"
#include "alamo/ui/gui_info_screens.hpp"
#include "alamo/ui/gui_menus.hpp"
#include "alamo/ui/gui_screens.hpp"  // BattleEndInfo
#include "alamo/ui/hud_markers.hpp"
#include "alamo/ui/hud_panels.hpp"
#include "alamo/ui/targeting.hpp"
#include "game_display.hpp"  // DisplayMode
#include "game_ui.hpp"
#include "screen_flow.hpp"   // Screen

namespace alamo::sim {
struct BattleStats;
}

namespace alamo_game_app {

// One unit of the running battle: the movement system's units, live in the tactical store.
struct ProbeUnit {
    alamo::objects::ObjectId id = alamo::objects::kNoObjectId;
    int32_t owner = -1;
    alamo::Vec3 position{};
    bool fights = false;  // the combat system has it
};

// The script run's bookkeeping. The App owns it: its step runner, PNG readback, bench PNGs and screenshot hotkey read
// and write it too; the script reaches it through AppProbe::control().
struct ScriptControl {
    std::vector<std::string> notes;  // printed at the end of the run
    bool smoke_ok = true;            // false: the run reports failure at its end
    bool failed = false;             // the script cannot go on: the run stops
    std::string shot_path;           // set: the next rendered frame is read back into this PNG
    bool shot_done = false;
    bool hold_sim = false;      // presentation-only pause (screenshots settle; never changes what the sim computes)
    bool hold_release = false;  // hold_sim ends before the NEXT script step runs (that step keeps its frame's ticks)
    bool script_hold = false;   // the same for a whole stretch of script steps (step_shot leaves it alone)
};

class AppProbe {
public:
    virtual ~AppProbe() = default;

    // ---- the battle (sim reads) ----
    // A battle is running (the session has a tactical battle).
    virtual bool in_battle() const = 0;
    // The battle's movement system has the unit (false without a battle).
    virtual bool unit_moves(alamo::objects::ObjectId id) const = 0;
    // The battle's combat system has the unit (false without a battle).
    virtual bool unit_fights(alamo::objects::ObjectId id) const = 0;
    // Players a and b are enemies in the battle's combat rules (false without a battle).
    virtual bool hostile(int32_t a, int32_t b) const = 0;
    // The battle's movable units, ascending id, live only; empty without a battle.
    virtual std::vector<ProbeUnit> battle_units() const = 0;
    // The battle's counters (shots, hits, deaths, ...); null without a battle.
    virtual const alamo::sim::BattleStats* battle_stats() const = 0;

    // ---- the script's own bookkeeping ----
    virtual ScriptControl& control() = 0;
    virtual uint64_t frames() const = 0;  // frames presented so far
    virtual double now_s() const = 0;     // wall seconds since start (the presentation clock)

    // ---- screen and dialogs ----
    virtual Screen screen() const = 0;
    virtual const alamo::game::GameUi& ui() const = 0;  // the dialogs, HUDs, text and the HUD type table as drawn
    // Scrolls list `control` of the open dialog `dialog` so row `row` is visible (false: no such dialog).
    virtual bool scroll_list_to(const std::string& dialog, const std::string& control, int32_t row) = 0;
    // Sets the selected row of list / combo `control` of the open dialog `dialog` (no click; false: no such control).
    virtual bool set_selected_row(const std::string& dialog, const std::string& control, int32_t row) = 0;
    virtual bool menu_paused() const = 0;       // the pause menu holds the session
    virtual bool menu_dialog_open() const = 0;  // the in-game menu or one of its option / save / load / quit dialogs
    virtual bool choice_open() const = 0;       // the galactic battle choice (fight / retreat / auto-resolve) is shown
    virtual bool battle_end_open() const = 0;   // the battle-end screen holds the ticks
    virtual bool story_summary_open() const = 0;
    virtual bool summary_auto_open() const = 0;  // the story summary opened by itself
    virtual bool campaign_over() const = 0;
    virtual const alamo::ui::InfoScreens& info() const = 0;            // holocron / droid log / summaries
    virtual const alamo::ui::EncyclopediaPanel& encyclopedia() const = 0;
    virtual uint64_t encyclopedia_shown() const = 0;                   // encyclopedia pop-ups shown so far
    virtual const std::vector<alamo::ui::GalacticScreenText>& screen_text() const = 0;  // this frame's galactic text feed
    virtual const std::vector<alamo::game::DisplayMode>& display_mode_list() const = 0;
    virtual uint64_t holocron_updates() const = 0;
    virtual size_t holocron_chapters() const = 0;  // story chapters the holocron lists
    virtual bool market_open() const = 0;          // the black market panel
    // Front-end and info-screen actions, as their buttons do them.
    virtual void menu_action(const alamo::ui::MenuAction& action) = 0;
    virtual bool show_battle_end(const alamo::ui::BattleEndInfo& info) = 0;  // false: the dialog is missing
    virtual void open_holocron(alamo::ui::InfoPage page) = 0;
    virtual void open_story_summary() = 0;
    virtual void show_campaign_end(bool victory) = 0;
    virtual void quit() = 0;  // the session ends and the app quits (ScreenEvent::Quit)

    // ---- HUD and orders ----
    virtual const std::vector<alamo::ui::HudUnit>& hud_units() const = 0;  // the tactical HUD's units of the last build
    virtual void hud_actions(const std::vector<alamo::ui::HudAction>& acts) = 0;  // as the HUD's buttons do them
    virtual uint32_t hover_ship() const = 0;          // the hovered ship (reticle owner), 0 none
    virtual int32_t hover_component() const = 0;     // the routed input's hovered HUD component
    virtual int32_t mouse_x() const = 0;
    virtual int32_t mouse_y() const = 0;
    virtual const alamo::ui::TargetingController& targeting() const = 0;  // the targeted-ability pick
    virtual const std::vector<uint32_t>& selection() const = 0;           // the local player's selection
    virtual void clear_selection() = 0;
    virtual void submit_command(alamo::commands::EventData ev) = 0;  // a command from the local player (voice reply on)
    virtual const alamo::ui::BuyUnitsState& buy() const = 0;           // the skirmish buy screen
    virtual const std::map<std::string, uint32_t>& bought() const = 0;  // type -> count (skirmish purchases)
    virtual uint64_t refused() const = 0;                               // commands the session refused
    virtual uint64_t reinforce_drops() const = 0;
    virtual bool reinforce_available(alamo::objects::ObjectId galactic_id) const = 0;
    virtual const std::vector<alamo::ui::HudWaypointLine>& waypoints() const = 0;  // the selection's path lines
    virtual const std::vector<alamo::ui::HudOrderQueue>& queues() const = 0;
};

}  // namespace alamo_game_app
