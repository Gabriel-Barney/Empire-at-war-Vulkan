// P2 tactical HUD (legacy docs/re/UI.md §1, §2): owns the canvas, the CommandBarComponents-driven layout, the
// world-space layer (hud_world.hpp), the selected-unit panel (s_select_NN / s_health_NN / s_shield_NN), the
// ability buttons (special_button_NN + special_border_NN), the tactical build queue (tqueueNN), the land
// reinforcement pad (r_RRCC), the command / game buttons, tooltips, the radar, order feedback (waypoint lines,
// move acknowledgements, hard-point reticles), the top-bar credits text, the HUD hit boxes and the HUD actions.
// Each frame:
//   resize(w, h) on a resolution change (canvas + three ortho cameras rebuilt, fonts re-rasterised);
//   route input with InputRouter (hud_input.hpp) using hit_test() as RouteHooks::hud_hit, then
//   update_input(routed, ...) -> commands::EventData (Select / SetUnitAbilityMode / SetAbilityAutofire /
//   Production cancel / Retreat / MoveToPosition) + HudAction host requests (targeting modes, camera jumps,
//   menu buttons); then
//   build(frame, units, types) -> draw_list() (13 layers) + selection_blobs() (3D selection circles).
// Read-only towards the sim: selection / health / abilities / queues come in as copies or through the
// HudProviders callbacks (until the S9 ability state lands); actions leave as commands.
//
// Shells (stage 2): load the .alo shells with ShellSet (hud_shell.hpp), apply() them to layout(), set
// TacticalHudFrame::shells_loaded and draw them with alamo_ui_draw ShellDrawer at the ShellModels step. Without
// them a [port] 2D frame stands in for the radar and the command bar and components sit at the [port]
// fallback positions (apply_tactical_fallback_layout).
#pragma once
#include <cstdint>
#include <array>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "alamo/commands/command.hpp"
#include "alamo/commands/fleet_group.hpp"  // [port] fleet group tab buttons (#177)
#include "alamo/ui/hud_buttons.hpp"
#include "alamo/ui/hud_canvas.hpp"
#include "alamo/ui/hud_components.hpp"
#include "alamo/ui/hud_draw.hpp"
#include "alamo/ui/hud_input.hpp"
#include "alamo/ui/hud_markers.hpp"
#include "alamo/ui/hud_radar.hpp"
#include "alamo/ui/hud_world.hpp"
#include "alamo/ui/orders.hpp"
#include "alamo/ui/upgrade_sidebar.hpp"

namespace alamo::ui {

class FontCache;

struct SelectedGroup {
    uint32_t type = 0;
    std::vector<uint32_t> ids;  // ascending
    float health = 1.0f;        // mean health fraction
    float shield = -1.0f;       // mean shield fraction (< 0: none)
    uint16_t fleet = 0;         // [port] fleet group id of the units (0: none)
    bool overflow = false;      // [port] the "+N" entry: every unit past the bar's last slot (type unused)
};
// Selected units grouped by type, groups ordered by their lowest id [port], at most `max_groups`.
std::vector<SelectedGroup> group_selection(std::span<const HudUnit> units, size_t max_groups = 48);

// [port: Gabriel] Fleet groups (docs/DECISIONS.md "Fleet groups: exception to the current focus"): one of the local
// player's groups, host feed per frame while fleet groups are on (#174).
struct HudFleetGroup {
    uint16_t id = 0;                // shown as the group's number
    std::u32string name;            // "" = unnamed
    bool locked = false;
    std::vector<uint32_t> members;  // every member, ascending (selected or not)
    float health = -1.0f;           // #600 aggregate health 0..1 (the members' mean hull fraction); < 0 unknown
    // #603 the members a running locked move is waiting for (host: TacticalBattle::fleet_lagging), ascending; empty while
    // no move runs or nobody lags
    std::vector<uint32_t> lagging;
};
// #179: one member's destination slot of a locked group move being aimed (host feed: TacticalBattle::fleet_move_preview).
struct HudFleetGoal {
    uint32_t id = 0;  // the member
    float x = 0.0f, y = 0.0f;
};
// #616 a shown slot of the hero bar (HeroPanel::shown_slots): its hero and the slot's box in pixels (top-left origin), with
// the layer the bar draws it on. Modern badges the heroes in a group or wing over these.
struct HudHeroSlot {
    uint32_t hero = 0;
    uint32_t layer = 0;
    Vec2f min_px{}, max_px{};
};
inline constexpr int32_t kFleetTabHitBase = 0x41000000;  // hit-box ids of the group header tabs (index into the runs)
// #177: a tab's Merge button (on the oldest tab when the selection spans two or more groups; sends FleetGroupOp::Merge)
// and Split button (on a tab whose group is only partly selected; sends FleetGroupOp::Create of the selection).
inline constexpr int32_t kFleetMergeHitBase = 0x42000000, kFleetSplitHitBase = 0x43000000;
// [port: Gabriel, 2026-11-22, #172] The selected-unit bar with fleet groups: each group's selected members by type
// (groups in feed order, types by their lowest id), then the ungrouped selected units by type. With more entries than
// `slots`, the entries from the last slot on fold into one overflow entry ("+N", every unit not shown).
std::vector<SelectedGroup> group_selection_fleets(std::span<const HudUnit> units, std::span<const HudFleetGroup> fleets,
                                                  size_t slots);
// [port] #588 the fleet group tabs both HUD styles draw. Per feed group, its selected members (`units` selected).
std::vector<size_t> fleet_selected_counts(std::span<const HudUnit> units, std::span<const HudFleetGroup> fleets);
// The tab of group `id`: the feed group (null: not in the feed), and its buttons - Merge on the oldest tab when the
// selection spans two or more groups, Split on a tab whose group is only partly selected.
struct HudFleetTabState {
    const HudFleetGroup* group = nullptr;
    bool merge = false, split = false;
};
HudFleetTabState fleet_tab_state(std::span<const HudFleetGroup> fleets, std::span<const size_t> selected, uint16_t id);
// A tab's left click: Select of its members; with Shift added to `selection` (at most 127).
std::vector<uint32_t> fleet_tab_select_ids(std::span<const uint32_t> members, std::span<const uint32_t> selection, bool shift);
// [port: Gabriel] #178 the ground outline of a fleet group (shared by the Classic outline and the Modern outline tag,
// #602): the circle round its live members (`units` ascending id) - their mean position, the farthest member's distance
// plus its select box, plus a 12-unit margin. None without a live member.
struct FleetOutline {
    Vec3 centre{};
    float radius = 0.0f;
};
std::optional<FleetOutline> fleet_outline(const HudFleetGroup& g, std::span<const HudUnit> units, std::span<const HudTypeInfo> types);
// [port: Gabriel] #599 (#596) fleet discipline: the player's fleet cohesion (per player) and the lock of the selection's
// fleet groups (per group) shown as one cycle for the selection, Loose -> Tight -> Locked, through the existing orders.
// Shown: Locked when fleet groups are on, the selection holds members of a group and every such group is locked; else
// Tight when the cohesion is Tight; else Loose. Next: Loose -> Tight sets cohesion Tight; Tight -> Locked locks the
// groups with selected members (SetLock true), or with no such group / fleet groups off Tight -> Loose sets cohesion
// Loose; Locked -> Loose unlocks them (SetLock false) and sets cohesion Loose, in two orders. Ungrouped selected units
// are not touched by the lock. `selection` = the selected own unit ids (any order).
enum class FleetDiscipline : uint8_t { Loose, Tight, Locked };
struct FleetDisciplineStep {
    FleetDiscipline shown = FleetDiscipline::Loose, next = FleetDiscipline::Tight;
    bool lock_available = false;    // fleet groups on and a group with selected members: Locked is a step
    bool selection_locked = false;  // ... and every such group is locked (the Lock key's SetLock(!selection_locked))
    std::optional<bool> lock;       // the next step's SetLock (true = lock), none = no lock order
    std::optional<bool> cohesion;   // the next step's cohesion (true = Tight), none = no cohesion change
};
FleetDisciplineStep fleet_discipline(bool fleet_groups_on, std::span<const HudFleetGroup> groups, std::span<const uint32_t> selection,
                                     bool tight);
// The fleet group SetLock order of the selection's groups (the Lock key Y, commands/fleet_group.hpp).
commands::EventData fleet_lock_event(bool locked);
// The "+N" fold both selection bars use (#585): with more entries than `slots`, the entries from the last slot on become
// one overflow entry (every unit not shown, ascending; health -1).
void fold_selection_overflow(std::vector<SelectedGroup>& groups, size_t slots);
// A left click on a selection portrait / card (#585, shared by both HUD styles): the ids of the Select command - the
// group; with Shift the selection without the group; with Ctrl the group's first unit - at most 127.
std::vector<uint32_t> portrait_select_ids(const SelectedGroup& group, std::span<const uint32_t> selection, bool shift, bool ctrl);

// Ability state of a selected group: read-only S9 queries (hud_abilities.hpp abilities_from_s9 fills it from
// tactical::abilities::AbilitySystem) or any host callback.
struct HudAbility {
    uint8_t ability_type = 0;   // 1..76 (legacy docs/re/COMMANDS.md §2 TacticalSpecialAbility)
    std::string icon;           // atlas texture (e.g. "i_sa_*.tga")
    float recharge = 1.0f;      // 0..1, 1 = ready (the dial shows recharge x 100 while < 1)
    float seconds_left = -1.0f;  // [port] #586 the recharge's seconds left (< 0 unknown): the Modern ring's count
    std::string type_name;       // [port] #586 the ABILITIES_HARDPOINTS 2.1 type name ("" unknown): the Modern key badge
    bool active = false;        // mode on (selected look)
    bool enabled = true;
    bool targeted = false;      // needs a target: a click starts the targeting mode (HudAction)
    uint8_t target_kind = 0;    // targeting.hpp TargetKind (0 = by ability_target_kind); BeginAbilityTargeting `value`
    bool autofire = false;      // autofire on (overlay shown); right click toggles
    std::string tooltip_key;    // MasterTextFile key for the tooltip ("" = the component's)
};
struct HudQueueEntry {
    uint16_t type_index = 0;
    std::string icon;
    float progress = 0.0f;      // 0..1 (Build_Texture_Name dial)
    uint32_t count = 1;
    uint32_t location = commands::kNoObject;  // Production event location
    uint8_t queue = 0, index = 0;
    bool land = false;          // galactic: ground queue (queue05..09, Build_Queue_Land) instead of space (queue00..04)
};
// One tactical buy button (Build_00..05, group "Tactical Build Buttons"): a type the selected builder (space station,
// build pad) can produce now. Host feed per frame from the builder's Tactical_Buildable_Objects_* list.
struct HudBuildOption {
    enum class Block : uint8_t {
        None,           // buyable
        QueueFull,      // Tooltip_Text[0] TEXT_PRODUCTION_QUEUE_FULL
        PopCap,         // Tooltip_Text[1] TEXT_PRODUCTION_POP_CAP_REACHED
        Funds,          // Tooltip_Text[2] TEXT_PRODUCTION_INSUFFICIENT_FUNDS
        Prerequisite,   // tech / prerequisite missing [port: disabled, no tooltip]
        Limit,          // build limit reached [port: disabled, no tooltip]
    };
    uint16_t type_index = 0;    // Production event type
    std::string icon;
    int32_t cost = -1;          // credits (text2, gold, under the button); < 0 none
    uint32_t queued = 0;        // entries of this type in the builder's queue (text, > 1 shown)
    Block block = Block::None;  // != None: disabled
    std::string tooltip_key;    // the type's text (Text_ID) shown as the tooltip of a buyable option
};
struct HudReinforcement {
    uint32_t unit = 0;          // reinforcement-pool object (Reinforce event "unit")
    std::string icon;
    uint32_t count = 1;
    bool available = true;
    // [port] #645 (fleet groups on): the fleet group the reserve joins on arrival, as a short label under the pad (the
    // pick, else what the galactic-fleet rule gives); empty with fleet groups off. A right click on the pad asks the host
    // for the next choice (HudAction::ReinforcementGroup).
    std::u32string group;
};
struct HudProviders {
    std::function<void(const SelectedGroup& group, std::vector<HudAbility>& out)> abilities;
    std::function<void(std::vector<HudQueueEntry>& out)> build_queue;
    std::function<void(std::vector<HudReinforcement>& out)> reinforcements;
};
// [port] One ability button of the selection (#586, shared by both HUD styles): the ability and every selected unit of
// the groups having it (sorted).
struct HudAbilityButton {
    HudAbility ability;
    std::vector<uint32_t> users;
};
// The selection's ability buttons: per group (panel order), its abilities (`provider`); distinct ability types take one
// button each, in first-seen order, at most `max` (merged: ready when any group is, active / enabled when any is; the
// seconds left of the readiest group, the shorter on a tie).
std::vector<HudAbilityButton> selection_ability_buttons(std::span<const SelectedGroup> groups, const HudProviders& providers,
                                                        size_t max);

// A request the HUD cannot turn into a command by itself (the host / targeting controller / P4 handle it).
struct HudAction {
    enum class Kind : uint8_t {
        Button,                 // a game button (component name): menu, camera, beacon, special weapon, speed, ...
        BeginAbilityTargeting,  // targeted ability chosen: ability_type, ids = users, value = TargetKind (-1 by type)
        BeginReinforcement,     // reinforcement chosen: unit (drop point picked in the world -> reinforce_event)
        RadarCameraJump,        // look at world (x, y)
        RadarPing,              // radar event to show (name) at (x, y), e.g. Default_Click
        // P4 galactic HUD (galactic_hud.hpp):
        GalacticSelect,         // fleet / land-force pad clicked: ids = objects to select (host -> Select command)
        PlanetInfo,             // planet_info / Planet name clicked: unit = planet object id
        // P4 panels (hud_panels.hpp), appended 2026-10-27:
        HeroSelect,             // hero roster slot: unit = hero object (host -> Select / camera)
        SabotageTarget,         // sabotage target: unit = target object, ids = {planet}
        CorruptionChoice,       // corruption option: unit = planet, value = corruption type
        PanelClose,             // *_close button / right click: name = the panel's shell component
        HackConfirm,            // hack_shell hack_hack button: unit = the hacked target, ids = {planet}
        BlackMarketBuy,         // P4 host, appended: black-market item: unit = item id, ids = {planet}, value = row
        ResumeGame,             // OI 2026-10-28, appended: "Resume Game" on the tactical Game Paused banner (pause_shell,
                                // attack_button): the host unpauses (pause_event(false)); name = "attack_button"
        BattleBegin,            // GALACTIC_DIALOGS §1.5, appended: "Begin" on the battle-pending prompt (choice_button_left /
                                // attack_button_single): host -> conflict state 3 (start the tactical battle), release pause
        BattleAutoResolve,      // "Auto-Resolve" (choice_button_right): host -> conflict state 6 with force, release pause
        // FIXPASS_GALACTIC §2 hero bar (hud_panels.hpp HeroPanel), appended 2026-10-28:
        HeroFocusPlanet,        // galactic left click: unit = hero, ids = {planet}: centre the galactic camera on the
                                // planet and select / focus it (fleet not selected, no panel)
        HeroZoomPlanet,         // galactic double click (after HeroFocusPlanet): unit = hero, ids = {planet}: zoom state
                                // 0 / 2 -> zoom into the planet; 1 / 3 -> zoom out (or close the advanced map)
        HeroSelectGroup,        // tactical double click (after HeroSelect): unit = hero, ids = {container} or {} = all
                                // objects of the hero's type; then centre the tactical camera on the hero
        // Tactical production (TacticalHud), appended 2026-10-28:
        TacticalBuy,            // Build_NN left click on an enabled option: unit = builder (TacticalHudFrame::builder),
                                // value = type_index, shift: host -> Production {type_index, location = builder}
        TacticalCancel,         // tqueueNN left click: unit = location, value = type_index, ids = {queue, index}: host ->
                                // Production {type_index, location, cancel = 1, queue, index}
        // ui4 t02, appended 2026-11-13: a left click on a greyed galactic build pad (GalacticBuildOption::block != None):
        // value = the Block, name = the pad component; the host plays the GUI negative feedback, no command is sent.
        BuildRefused,
        // [port: Gabriel] appended 2026-11-13: a double click on an own galactic organiser pad (fleet header or unit pad):
        // unit = the fleet object (header) or the pad's unit (a stack: its first unit), ids = {fleet}, value = 1 for a
        // fleet / 0 for a unit, name = the component name. Host: open the rename prompt (current name from the sim).
        Rename,
        // [port: Gabriel] appended 2026-11-13: a squadron wing button (HudWing) clicked: value = the wing index into
        // TacticalHudFrame::wings, ids = the wing's members, shift = add to the selection, ctrl = centre the camera only
        // [port]. Host: select them (shift: the selection plus them); a second click within the double-click time also
        // centres the camera on the wing.
        WingSelect,
        // [port: Gabriel] appended 2026-11-22 (#176): a right click on a fleet group's header tab in the selected-unit bar:
        // value = the group id, name = its name (UTF-8). Host: open the rename prompt for the group.
        FleetGroupRename,
        // [port: Gabriel] appended 2026-11-25 (#584, docs/DECISIONS.md "Modern HUD: exception to the current focus"): the
        // HUD style switch drawn on the Modern HUD was clicked. Host: switch Classic <-> Modern (presentation only).
        HudStyleToggle,
        // [port: Gabriel] appended 2026-11-25 (#599): the Modern fleet discipline control's cohesion step: value = 1 Tight,
        // 0 Loose. Host: GameSession::set_fleet_cohesion (the O key's toggle sets the same preference).
        FleetCohesion,
        // [port: Gabriel] appended 2026-11-26 (#645): a right click on a reinforcement pad while fleet groups are on: unit =
        // the reserve (Reinforce "unit"), name = the pad component. Host: the next group choice for it (the galactic-fleet
        // rule, each of the player's groups, no group), sent as the AssignReserve fleet group command.
        ReinforcementGroup,
    };
    Kind kind = Kind::Button;
    int32_t component = -1;
    std::string name;           // component name / radar event name
    uint8_t ability_type = 0;
    int32_t value = -1;         // panel-specific (CorruptionChoice type)
    uint32_t unit = 0;
    bool queued = false;        // [port: Gabriel] issued with the queue modifier (BeginAbilityTargeting: queue the order)
    std::vector<uint32_t> ids;
    float x = 0, y = 0;
    Button button = Button::Left;
    bool shift = false, ctrl = false;
};

// A 3D model the HUD wants drawn through an ortho camera (radar event models, legacy docs/re/UI.md §1.7: RadarMap.xml
// Event_Model_Name, e.g. I_minimap_*.alo). Model space -> canvas: translate(center) x uniform `scale` (canvas units per
// model unit); drawn by alamo_ui_draw HudModelDrawer at the Models2 step (HUD camera kHudCameraModels2).
struct HudModelDraw {
    std::string model;     // .alo name
    Vec2f center{};        // canvas units
    float scale = 1.0f;    // canvas units per model unit = Event_Model_Scale x canvas width / 2 [M: RadarMap.xml's logical
                           // screen space; the vanilla models are one 20-60 unit quad]
    float age = 0.0f;      // seconds since the event started (animation time)
    uint32_t event = 0;    // RadarEvent id
};

// One objective line (tactical Mission Objectives box, galactic objectives panel).
struct HudObjective {
    enum class State : uint8_t { Active, Complete, Failed };
    std::string text_key;               // MasterTextFile key
    std::u32string text;                // overrides the key when not empty
    State state = State::Active;
};

// An objective's line text: its own `text`, else the key's MasterTextFile entry (GUI escapes resolved), else the key
// itself without a text database. Shared by the Classic objectives box and the Modern objectives panel (#589).
std::u32string hud_objective_text(const TextDatabase* text, const HudObjective& o);

// [port: Gabriel] (#589, docs/DECISIONS.md "Modern HUD: exception to the current focus") one entry of the Modern HUD's
// alert strip: an advisor line or an event the host already reacts to. Presentation only (never read by the sim).
struct HudAlert {
    enum class Kind : uint8_t { Advisor, Radar, Production, Objective };
    Kind kind = Kind::Advisor;
    std::u32string text;
    bool has_position = false;  // Jump (HudAction::RadarCameraJump) goes to (x, y)
    float x = 0.0f, y = 0.0f;
    double time = 0.0;          // wall seconds when it was raised
    float age = 0.0f;           // seconds since then (HudAlertLog::update)
};

// Tactical order buttons c_button00..05 (attack, attack-move, move, waypoint, full stop, guard; legacy docs/re/HUD_GAPS.md §5):
// never hidden - visible whenever the tactical command bar is - only enabled / disabled every frame from the local
// player's OWN selected units (enemy / allied units in the selection are ignored):
//   attack      disabled when no own selected unit can attack (attack behaviour)
//   attack-move disabled when none can attack AND none can move
//   move, waypoint, full stop   disabled when none can move (locomotor)
//   guard       disabled when none can attack AND none can move
// controls_locked (story LOCK_CONTROLS / Lua Lock_Controls) disables all six. A Lua GUI_Component_Visibility /
// _Enable call on them (mods only) overrides this (ButtonRuntime::script_visible / script_enabled).
struct HudOrderButtons {
    bool can_attack = false;     // some own selected unit has an attack behaviour
    bool can_move = false;       // some own selected unit has a locomotor
    bool controls_locked = false;
};
inline constexpr uint32_t kOrderButtonCount = 6;  // c_button00..05
bool order_button_enabled(uint32_t button, const HudOrderButtons& s);  // button 0..5 = c_button00..05

// Land b_special_weapon = the bombing-run button (HUD_GAPS §9.1 - §9.3). Never hidden in land, only disabled
// every frame (controls locked, no bombing-run record, run not available); alternate icon 3, tooltip 3
// (TEXT_TOOLTIP_BOMBRUN_BUTTON); the Build_Texture_Name dial is the cooldown. `present` false (no tactical HUD feed): the
// component keeps its data state. In space the same component is the special-weapon button (HUD_GAPS §9.1,
// 0x317d00: icon 0..2 = the weapon's Special_Weapon_Index, a station 2; tooltip 0..2, or 4 TEXT_TOOLTIP_REQUIRES_COMM_ARRAY
// while special weapons are not allowed); `hidden` [port] when the local player has no space special weapon.
struct HudBombingRunButton {
    bool present = false;       // a battle: the HUD drives the button
    bool hidden = false;        // space without a special weapon
    int32_t icon = 3;           // Icon_Alternate_Texture_Name index (land 3)
    int32_t tooltip = 3;        // Tooltip_Text index (land 3)
    bool disabled = true;       // !ready (0x312570)
    float dial = -1.0f;         // 0..1 radial dial (Build_Texture_Name); < 0: none. [port] none when ready (the 0x495250 value
                                // 1.0 draws nothing, as the ability buttons; HUD_GAPS §9.9 item 1 is a probe question)
};

// the other command-bar buttons the land / space HUD update enables in the same pass as c_button00..05
// (HUD_GAPS §5.1: "reinforcement availability, retreat allowed") and the reinforcement window's texts. `present` false
// (no battle feed): the components keep their data state. Lua GUI_Component_Visibility / _Enable override (mods).
struct HudCommandButtons {
    bool present = false;
    bool controls_locked = false;    // story LOCK_CONTROLS: all of them disabled
    // b_retreat: enabled when the retreat request would be accepted (BATTLE_TRANSITIONS §3.4 vf9 reason 0). Its
    // Build_Texture_Name dial [port]: the retreat-allowed countdown while it runs (0..1), then the running retreat's
    // countdown; < 0 none.
    bool retreat_enabled = false;
    float retreat_dial = -1.0f;
    // b_reinforcement: enabled when some reinforcement-pad entry can be called (reserve_available); selected look while a
    // drop point is being picked.
    bool reinforce_enabled = false;
    bool reinforce_selected = false;
    // b_special_weapon2 (TEXT_PLACE_BEACON) / b_beacon_t: enabled when no beacon was placed within BeaconPlaceDelay;
    // selected while the beacon point is being picked.
    bool beacon_enabled = false;
    bool beacon_selected = false;
    bool movie_mode = false;         // b_camera_t (TEXT_MOVIE_MODE) selected look
    // reinforcement_counter = number of pad entries (hidden at 0); r_pop_text = "used/cap" population (hidden
    // when there is no cap: land); r_pop_icon shown with it.
    uint32_t reinforce_count = 0;
    int32_t pop_used = -1, pop_cap = -1;
};

// [port: Gabriel, 2026-11-13] Squadron wing buttons: the local player's fighter squadrons (and bomber squadrons) form one
// wing each; a column of buttons at the left edge selects a whole wing with one click (HudAction::WingSelect). Drawn
// only while the wing has a squadron. The original has no such buttons.
struct HudWing {
    std::u32string label;            // "FIGHTERS", "BOMBERS"
    std::u32string hotkey;           // key hint drawn after the label ("F2"); empty = none
    uint32_t squadrons = 0;          // squadrons in the wing (drawn as the count)
    std::vector<uint32_t> ids;       // every member of the wing's squadrons, ascending
    bool selected = false;           // the whole wing is in the selection (the selected look)
    // #600: the wing's craft alive / in full strength (its squadrons' Squadron_Units), its aggregate health 0..1 (the
    // craft's mean hull fraction; < 0 unknown).
    uint32_t craft_alive = 0, craft_total = 0;
    float health = -1.0f;
};
inline constexpr int32_t kWingHitBase = 0x40000000;  // hit-box ids of the wing buttons (not CommandBarComponents)
inline constexpr size_t kMaxWings = 4;
struct HudAction;
// A left click on wing button `wing` (#585, shared by both HUD styles): HudAction::WingSelect (component kWingHitBase + wing).
HudAction wing_select_action(int32_t wing, std::span<const uint32_t> ids, bool shift, bool ctrl);

// #477: see TacticalHudFrame::sell.
struct HudSellButton {
    uint32_t object = 0;
    Vec2f anchor_px{};       // the object's projected point (pixels, +Y down)
    std::u32string name;     // the type's name (Text_ID)
    int32_t refund = 0;      // TacticalBattle::sell_refund
};
// #664 generic_flytext (component 0x37d; 0x2de390 spawns, 0x305890 steps, 0x2ff5a0 places [H]): a short text that
// floats at a world point, e.g. the local player's kill bounty "%+d". Retail steps each record once per HUD update with
// age = game frame - start: past 60 frames it is removed; the alpha is (1 - max(age - 30, 0) / 30) x 255 (solid for 30
// frames, then a 30-frame fade); its 2D offset is (0, -70 x age / 60) added to the projected point in pixels (0x261e40's
// y runs top-down, scaled by +0x100c to pixels, then 0x352650), so the text rises 70 pixels.
inline constexpr int32_t kFlyTextFrames = 60;
struct FlyTextLook {
    bool alive = false;
    uint8_t alpha = 0;
    float rise = 0.0f;  // pixels up
};
FlyTextLook fly_text_look(int32_t age);
struct HudFlyText {
    Vec2f anchor_px{};    // the world point's projected pixel (+Y down)
    float rise = 0.0f;    // fly_text_look
    uint8_t alpha = 255;
    std::u32string text;  // "%+d"
};

// 0x2dd250's caption, L" %s %s ($ %d) ": the localized TEXT_BUTTON_SELL, the type's name and the refund.
std::u32string tactical_sell_text(std::u32string_view sell, std::u32string_view name, int32_t refund);

// [port] #592 a land reinforcement zone for the Modern map (TACTICAL_GAPS §4.2.1 test 4, check-radius drops): an allied
// object's Reinforcement_Enabling_Radius (with Use_Reinforcement_Points: drops are allowed inside it) or a non-allied
// object's Reinforcement_Prevention_Radius (drops are refused inside it). World units.
struct HudDropZone {
    float x = 0.0f, y = 0.0f, radius = 0.0f;
    bool enabling = true;  // false: a prevention radius
};

struct TacticalHudFrame {
    WorldHudContext world;          // canvas is filled by TacticalHud
    // Radar mapping: world rectangle shown by the radar (the map's playable box).
    float map_min_x = -5000, map_min_y = -5000, map_max_x = 5000, map_max_y = 5000;
    int64_t credits = -1;           // < 0: no credits text [port: retail would show a negative amount]; else "$ 13,000" (hud_credits_text)
    // [port: Gabriel, original screenshot] planet panel above the radar: Text_Planet_tactical (battle planet name),
    // Text_Tactical_Tech ("Tech: 3", TEXT_TECH), Text_Planetary_Pop ("13/20"; Text_Pop_Cap without it); empty / < 0 = blank.
    std::u32string planet_name;
    std::u32string cohesion_text;  // [port: Gabriel 2026-11-13] fleet cohesion line above the planet name ("" = none)
    bool cohesion_tight = false;   // #599 the local player's fleet cohesion is Tight (with cohesion_text: a space battle)
    int32_t tech = -1, pop = -1, pop_cap = -1;
    // FIXPASS_GALACTIC §4.2 bottom-left advisor frame (i_main_skirmish = i_tactical_controls.alo bones: Movie_tactical
    // (50, 288), b_droid_help_tactical (19, 255), Text_tactical_help (65, 309), Text_Planet_tactical (179, 239); the
    // faction frame model b_Help_Droid_{Empire,Rebel,Underworld}_ALT{0,1,2} sits at (45.7, 287.5) behind the movie and is
    // drawn by the shell drawer). Movie_tactical = an Icon, Size 200 x 200 with the bone at its bottom-left corner (capture
    // C2), showing the advisor *_LOOP Bink (a talking head, not a static image). Host feed: the image page holding the loop's current frame, only while an advisor loop
    // plays (-1 = none: the movie component stays hidden [M]), and Movies.xml Alpha (true: straight alpha, PrimAlpha).
    int32_t advisor_page = -1;
    bool advisor_alpha = true;
    uint8_t faction_alt = 1;        // index into per-faction component lists (Blank_Texture_Name: 0 rebel, 1 empire) [port]
    bool shells_loaded = false;     // shell models drawn by alamo_ui_draw: no [port] 2D placeholder frames
    double time = 0.0;              // wall seconds (tooltip delay, blink, flash)
    HudProviders providers;
    // Radar (stage 2): the dynamic picture (terrain + FOW, uploaded by the renderer as page kHudPageRadar; null =
    // backdrop only), pings, camera footprint (ground plane z).
    const RadarImage* radar_image = nullptr;
    const RadarEvents* radar_events = nullptr;
    const RadarSettings* radar_settings = nullptr;
    bool radar_footprint = true;
    // true: the host draws radar_models() (HudModelDrawer) - pings whose event type has an Event_Model_Name then get no
    // [port] dot-ring stand-in; false: every ping draws the ring.
    bool radar_event_models = false;
    float ground_z = 0.0f;
    bool radar_colorize_selected = true;          // GameConstants Radar_Colorize_Selected_Units
    Rgba8 radar_selected_color{209, 255, 209, 255};
    // Order feedback.
    std::span<const HudWaypointLine> waypoints;
    // Tactical buy buttons (Build_00..05): the selected builder (kNoObject = none: no buttons) and its options (first
    // six). Placement: the i_build_buttons.alo bone grid (Build_00..02 at y -67, Build_03..05 at y -120; x -54 / -0.5 /
    // 52, canvas units) around build_anchor_px (the builder's projected pixel, e.g. a build pad), or, with no anchor,
    // around the bottom-centre [port] point (0, -H/2 + 430) above the build queue.
    uint32_t builder = commands::kNoObject;
    std::span<const HudBuildOption> build_options;
    bool has_build_anchor = false;
    Vec2f build_anchor_px{};
    // Hard-point reticles (HUD_GAPS §1): the hard points of the hovered ship, or of every capital ship with
    // HudHardPoint::parent set - only the hovered one's are drawn (filter_hardpoints). `images` resolves the stand-alone I_Hard_Point_Reticle_* DDS textures
    // to image pages (the GUI's resolver; null = [port] corner outlines).
    std::span<const HudHardPoint> hardpoints;
    std::span<const HudDropZone> drop_zones;  // #592 land: the reinforcement zones the Modern map marks
    const ReticleTable* reticles = nullptr;
    const GuiImageResolver* images = nullptr;
    const MoveAcks* move_acks = nullptr;
    // Tooltips: cursor pixel for placement.
    Vec2f cursor_px{};
    // [port: Gabriel] sim paused (the pause button's selected look; a click emits pause_event(!paused), orders.hpp) and
    // the queued orders of the units to mark (sim queued_orders(unit), hud_markers.hpp add_queue_markers).
    bool paused = false;
    std::span<const HudOrderQueue> order_queues;
    HudOrderButtons order_buttons;  // c_button00..05 enable state (HUD_GAPS §5)
    HudBombingRunButton bombing_run;  // b_special_weapon in land (HUD_GAPS §9)
    // #474 garrison_respawn_counter (0x231): the local player's skirmish start-force re-delivery fill 0..1 (the sim's
    // garrison_respawn), drawn as the component's Build_Texture_Name dial; < 0 hides it.
    float garrison_respawn = -1.0f;
    HudCommandButtons command_buttons;  // b_retreat, b_reinforcement, beacon, movie mode, reinforcement texts
    // [port] #587 for the Modern deck: whether Classic's reinforcement list is open (TacticalHud::reinforce_list_open: the
    // b_reinforcement button / Reinforce key toggle it), and the local player's station to select with nothing selected
    // (kNoObject: none).
    bool reinforce_list_open = false;
    uint32_t station = commands::kNoObject;
    // #608 for the Modern deck's Idle shortcut (nothing selected): the local player's idle units to select
    // (TacticalBattle::idle_units, whole squadrons, at most OrderTranslator::kMaxSelect ids) and the count the shortcut shows (a
    // squadron or company once). Empty / 0: no shortcut.
    std::span<const uint32_t> idle_units;
    uint32_t idle_count = 0;
    bool objectives_hidden = false;     // ui3 t01 [port]: the Mission Holocron key (Tab) toggles the objectives box
    // [port: Gabriel, original screenshot] Game Paused banner (pause_shell: yellow TEXT_GAME_PAUSED bar at the top
    // centre, red TEXT_BUTTON_RESUME_GAME button under it - a click emits HudAction::ResumeGame) while `paused`.
    bool pause_banner = true;
    // [port: Gabriel, original screenshot] Mission Objectives box at the bottom right (objective_back / _header_text /
    // _text / _icon components): header (TEXT_TEST_OBJECTIVE_HEADER "Mission Objectives"), a rule, then the numbered
    // objectives word-wrapped to the box. Empty = no box.
    std::span<const HudObjective> objectives;
    std::u32string objectives_header;   // "" = TEXT_TEST_OBJECTIVE_HEADER
    // [port: Gabriel] #589: the Modern alert strip's entries (HudAlertLog, oldest first); the Classic HUD ignores them.
    std::span<const HudAlert> alerts;
    // Drag-select box (legacy docs/re/UI.md §1.1 step 5h "drag rectangle"; Playtest 3): while the world owns a left drag past the drag
    // threshold (InputState ButtonState::dragging), the press -> cursor rectangle in pixels (ButtonState::drag). Drawn
    // by add_drag_box over the same bounds box_select uses on release (drag_select_bounds). The corner marker of the
    // original is the POINTER_DRAG_SELECT_MODE cursor (DragSelectPointer_00), drawn by the cursor pass, not here.
    bool drag_select = false;
    DragRect drag_rect;
    std::span<const HudWing> wings;  // [port: Gabriel] squadron wing buttons (HudWing); empty = none
    // [port: Gabriel] fleet groups on in this battle, and the local player's groups (creation order). Off: the bar is the
    // retail one whatever the feed holds.
    bool fleet_groups_on = false;
    std::span<const HudFleetGroup> fleet_groups;
    std::span<const HudFleetGoal> fleet_preview;  // #179: faint circles at these slots (fleet groups on only)
    std::span<const HudHeroSlot> hero_slots;      // #616 the hero bar's shown good slots (read by Modern only)
    // #477 tactical_sell (0x217, TACTICAL_COMMANDBAR_COMPONENTS.md "Tactical sell"): the button the host opened over an own
    // object with a sell offer (object 0 = none), at its projected pixel; text tactical_sell_text(TEXT_BUTTON_SELL, name,
    // refund). A left click sends TacticalSell {object}.
    HudSellButton sell;
    std::span<const HudFlyText> fly_texts;  // #664 generic_flytext records in front of the camera
    // #478 skirmish_upgrade (0x232): what the sidebar shows (nullptr: no sidebar), and GameConstants Icons_Per_Column.
    const SidebarInput* sidebar = nullptr;
    int32_t icons_per_column = 13;
};

struct TacticalHudStats {
    WorldHudStats world;
    uint32_t panel_slots = 0, radar_blips = 0, hit_boxes = 0, quads = 0;
    uint32_t ability_buttons = 0, queue_entries = 0, reinforcements = 0, buttons = 0, radar_events = 0;
    uint32_t build_options = 0;                  // Build_NN buttons drawn
    uint32_t waypoint_dots = 0, reticles = 0, ack_dots = 0, queue_dots = 0, queue_markers = 0;
    uint32_t radar_icons = 0, radar_models = 0;  // blips drawn with Radar_Icon_Name; pings handed out as models
    bool advisor_movie = false;                  // Movie_tactical drew the advisor loop this frame
    bool tooltip = false;
    bool pause_banner = false;          // Game Paused banner drawn
    bool sell_button = false;           // #477 tactical_sell button drawn
    uint32_t fly_texts = 0;             // #664 generic_flytext texts drawn
    uint32_t sidebar_slots = 0;         // #478 skirmish_upgrade slots drawn
    uint32_t objectives = 0, objective_lines = 0;
    bool drag_box = false;              // drag-select box drawn
    uint32_t wings = 0;                 // [port: Gabriel] squadron wing buttons drawn
    uint32_t fleet_tabs = 0;            // [port: Gabriel] fleet group header tabs drawn whose centre hits them (#598)
    uint32_t fleet_outlines = 0;        // [port: Gabriel] locked fleet group ground outlines (#178)
    uint32_t fleet_preview = 0;         // [port: Gabriel] move-preview circles (#179)
};

class TacticalHud {
public:
    // Tables must outlive the HUD. `text` may be null (keys shown as text).
    void init(const ComponentTable& table, const TextureAtlas& atlas, FontCache& fonts, const TextDatabase* text = nullptr);
    void resize(uint32_t width, uint32_t height);
    // [port: Gabriel] DPI-style UI scale (hud_canvas.hpp; 1 = 100 %, 0 = the original fitted canvas): takes effect on the
    // next resize().
    void set_ui_scale(float s) { canvas_.ui_scale = s > 0.0f ? s : 0.0f; }
    const HudCanvas& canvas() const { return canvas_; }
    HudLayout& layout() { return layout_; }
    const HudLayout& layout() const { return layout_; }
    const ComponentTable& components() const { return *table_; }
    // #477: the tactical_sell component (-1 = none in the data): the host's sell cursor over it.
    int32_t sell_component() const { return sell_; }
    HudButtons& buttons() { return buttons_; }
    // ui2 t02 (FLASH_SPECIAL_ABILITY, REWARDS 3.6): the special_button_NN component bound to an ability type, -1 when none.
    int32_t ability_component(uint8_t ability_type) const {
        for (const AbilitySlot& s : ability_slots_)
            if (s.component >= 0 && s.ability.ability_type == ability_type) return s.component;
        return -1;
    }
    const HudButtons& buttons() const { return buttons_; }
    void set_sfx_hook(HudSfxHook hook) { buttons_.set_sfx_hook(std::move(hook)); }
    void set_tooltip_config(const TooltipConfig& c) { tooltip_cfg_ = c; }
    // #463 (0x2e5580 [H], docs/re/TACTICAL_HOTKEYS.md "Reinforce"): the reinforcement list (component i_main_reinforce:
    // the r_RRCC pad, r_pop_icon / r_pop_text), closed at init. R and the b_reinforcement click toggle it: an open list
    // closes; a closed one opens only when `allowed` (the host's gates). The next build() draws the new state.
    enum class ReinforceListToggle : uint8_t { Closed, Opened, Refused };
    ReinforceListToggle toggle_reinforce_list(bool allowed);
    bool reinforce_list_open() const { return reinforce_list_open_; }
    void close_reinforce_list() { reinforce_list_open_ = false; }

    void build(const TacticalHudFrame& frame, std::span<const HudUnit> units, std::span<const HudTypeInfo> types,
               const FlyTextList* flytext = nullptr);
    const HudDrawList& draw_list() const { return dl_; }
    const std::vector<SelectionBlob>& selection_blobs() const { return blobs_; }
    const std::vector<SelectedGroup>& panel_groups() const { return groups_; }
    const TacticalHudStats& stats() const { return stats_; }
    const RadarView& radar_view() const { return radar_view_; }
    // Radar event models of the last build (pings inside the radar whose RadarMap.xml type names a model), in event order.
    const std::vector<HudModelDraw>& radar_models() const { return radar_models_; }
    const TooltipState& tooltip() const { return tooltip_; }
    // [port: Gabriel] (#584) World-space split for the Modern HUD style: while on, build() puts the world-space layer
    // (brackets, bars, fly text, waypoint lines, move acknowledgements, hard-point reticles, queued-order markers, the
    // drag box) into world_draw_list() instead of draw_list(), which then holds the Classic panels only. Off (the
    // default): world_draw_list() stays empty and draw_list() is the whole Classic HUD, exactly as before.
    void set_world_split(bool on) { world_split_ = on; }
    const HudDrawList& world_draw_list() const { return world_dl_; }

    // Topmost HUD component under a pixel (-1: none) - RouteHooks::hud_hit.
    int32_t hit_test(int32_t px, int32_t py) const;
    const std::vector<HudHitBox>& hit_boxes() const { return hits_; }

    // One frame of HUD input (stage 2): hover / press visuals, SFX hooks, clicks -> commands + host actions. Uses the
    // layout of the previous build() (what the player sees). `routed.world` gives the cursor for radar drags.
    void update_input(const RoutedInput& routed, bool shift, bool ctrl, double now, std::vector<commands::EventData>& out,
                      std::vector<HudAction>* actions = nullptr);
    // an ability hotkey = a left click on the first ability button (selection panel order) showing
    // `ability_type`: the same command / BeginAbilityTargeting action as the click (`queued` = the queue modifier).
    // False when no button shows it or it is disabled (nothing emitted). Uses the slots of the previous build().
    bool activate_ability(uint8_t ability_type, bool queued, std::vector<commands::EventData>& out,
                          std::vector<HudAction>* actions = nullptr) const;
    // c_button00..05 enabled after the previous build() (HUD_GAPS §5.1 rules, Lua overrides); false when the
    // component is missing or hidden. The order hotkeys (Attack, Move, ...) act only while their button would.
    bool order_button_live(uint32_t button) const;
    // Stage-1 entry point: left click on a selection portrait -> Select{that group's ids} (no state change).
    void on_pointer(std::span<const HudPointerEvent> events, std::vector<commands::EventData>& out) const;

private:
    void build_panel(const TacticalHudFrame& f, std::span<const HudTypeInfo> types);
    void build_abilities(const TacticalHudFrame& f);
    void build_queue(const TacticalHudFrame& f);
    void build_reinforcements(const TacticalHudFrame& f);
    void build_buy_buttons(const TacticalHudFrame& f);
    void build_buttons(const TacticalHudFrame& f);
    void build_radar(const TacticalHudFrame& f, std::span<const HudUnit> units, std::span<const HudTypeInfo> types);
    void build_skeleton(const TacticalHudFrame& f);
    void build_markers(const TacticalHudFrame& f, std::span<const HudUnit> units, HudDrawList& dl);
    void apply_order_buttons(const HudOrderButtons& s);
    void apply_bombing_run_button(const HudBombingRunButton& s);
    void build_garrison_respawn(const TacticalHudFrame& f);
    void build_sell(const TacticalHudFrame& f);
    void build_fly_texts(const TacticalHudFrame& f);
    void build_sidebar(const TacticalHudFrame& f);
    void apply_command_buttons(const HudCommandButtons& s);
    void build_pause_banner(const TacticalHudFrame& f);
    void build_objectives(const TacticalHudFrame& f);
    void build_wings(const TacticalHudFrame& f);
    void build_fleet_tabs(const TacticalHudFrame& f);
    void build_fleet_outlines(const TacticalHudFrame& f, std::span<const HudUnit> units, std::span<const HudTypeInfo> types);
    size_t panel_slot_count() const;
    int32_t font_of(int32_t component, float fallback_points) const;
    void hit(int32_t component, uint32_t layer, Vec2f center, Vec2f size, uint32_t slot = 0);
    Vec2f component_size(uint32_t component, const ComponentPlacement& p) const;
    std::vector<uint32_t> selection_ids() const;

    const ComponentTable* table_ = nullptr;
    const TextureAtlas* atlas_ = nullptr;
    FontCache* fonts_ = nullptr;
    const TextDatabase* text_ = nullptr;
    HudCanvas canvas_ = HudCanvas::make(1024, 768);
    HudLayout layout_;
    WorldHud world_;
    HudButtons buttons_;
    TooltipState tooltip_;
    bool tooltip_modal_ = false;  // #71 t06: the last build had a HUD modal up (no tooltip)
    TooltipConfig tooltip_cfg_;
    HudDrawList dl_;
    HudDrawList world_dl_;      // #584: the world-space layer while world_split_ (else empty)
    bool world_split_ = false;
    std::vector<SelectionBlob> blobs_;
    std::vector<SelectedGroup> groups_;
    std::vector<HudHitBox> hits_;
    std::vector<int32_t> slot_component_, health_component_, shield_component_;
    std::vector<int32_t> special_button_, special_border_, queue_component_, reinforce_component_;
    struct AbilitySlot : HudAbilityButton {
        int32_t component = -1;
    };
    std::vector<AbilitySlot> ability_slots_;
    struct QueueSlot {
        int32_t component = -1;
        HudQueueEntry entry;
    };
    std::vector<QueueSlot> queue_slots_;
    struct ReinforceSlot {
        int32_t component = -1;
        HudReinforcement entry;
    };
    std::vector<ReinforceSlot> reinforce_slots_;
    struct BuySlot {
        int32_t component = -1;
        HudBuildOption option;
    };
    std::vector<BuySlot> buy_slots_;
    std::array<int32_t, 6> buy_component_{-1, -1, -1, -1, -1, -1};
    uint32_t builder_ = 0;
    std::vector<uint8_t> dynamic_;  // per component: drawn by a panel (not by build_buttons)
    int32_t radar_ = -1, radar_blip_ = -1, credits_ = -1, shell_ = -1, retreat_ = -1;
    int32_t order_button_[6] = {-1, -1, -1, -1, -1, -1};  // c_button00..05
    int32_t garrison_respawn_ = -1;                       // garrison_respawn_counter (#474)
    int32_t flytext_ = -1;                                // generic_flytext (#664)
    int32_t sidebar_ = -1;                                // skirmish_upgrade (#478)
    UpgradeSidebar sidebar_list_;                         // its lists, kept across frames (HUD +0x1130 / +0x1148)
    int32_t sell_ = -1;                                   // tactical_sell (#477)
    uint32_t sell_drawn_ = 0;                             // the object of the sell button drawn this frame (0 = none)
    int32_t special_weapon_ = -1;                         // b_special_weapon (land: the bombing-run button)
    int32_t reinforce_button_ = -1, beacon_button_ = -1, beacon_t_ = -1, camera_t_ = -1;  // ui3 t03
    int32_t reinforce_counter_ = -1, r_pop_icon_ = -1, r_pop_text_ = -1;                 // ui3 t04
    bool reinforce_list_open_ = false;                                                   // #463
    int32_t planet_text_ = -1, tech_text_ = -1, pop_text_ = -1, movie_ = -1;
    int32_t pause_ = -1, pause_g_ = -1;  // [port: Gabriel] b_play_pause_t / b_play_pause
    bool paused_ = false;
    int32_t pause_text_ = -1, resume_ = -1;  // pause_shell text_attack / attack_button
    int32_t obj_back_ = -1, obj_header_ = -1, obj_text_ = -1, obj_icon_ = -1;
    std::vector<std::vector<uint32_t>> wing_ids_;  // [port: Gabriel] members per wing button of the last build
    int32_t wing_hover_ = -1;                      // wing button under the cursor (index), -1 none
    // [port: Gabriel] fleet groups (#174): every member per header tab of the last build, and each drawn panel slot's
    // rectangle (centre, size, layer; size 0 = not drawn).
    std::vector<std::vector<uint32_t>> fleet_tab_ids_;
    std::vector<std::pair<uint16_t, std::string>> fleet_tab_group_;  // per tab: the group id and name (UTF-8)
    std::vector<size_t> fleet_selected_;  // per feed group: its selected members (this build)
    struct SlotRect {
        Vec2f center{}, size{};
        uint32_t layer = 0;
    };
    std::vector<SlotRect> slot_rects_;
    RadarView radar_view_;
    std::vector<HudModelDraw> radar_models_;
    bool radar_drag_ = false;
    float radar_ground_z_ = 0.0f;
    uint8_t faction_alt_ = 1;
    TacticalHudStats stats_;
};

// Drag-select box (TacticalHudFrame::drag_select). Colours / outline width are not in docs/re: [inferred] from the
// base-game screenshot (Playtest 3, 41.jpg): a faint grey translucent fill and a 1-pixel light-grey outline, both
// inside drag_select_bounds(rect) (the outline covers the press / cursor pixel rows and columns).
inline constexpr Rgba8 kDragBoxFill{128, 128, 128, 64};       // [inferred]
inline constexpr Rgba8 kDragBoxOutline{176, 176, 176, 255};   // [inferred]
inline constexpr uint32_t kDragBoxLayer = 9;                  // [inferred] over the shells / panels, under tooltips (10)
// Appends the fill and the four outline quads (Alpha list, white page) to `dl` on `layer`; returns the quads added
// (0 for an empty canvas). Fill first, then top, bottom, left, right edges; edges collapse into the fill when the box
// is narrower than two pixels.
uint32_t add_drag_box(HudDrawList& dl, const HudCanvas& canvas, const DragRect& rect, uint32_t layer = kDragBoxLayer);

// Decimal with thousands separators ("13,000", "-1,250") (original screenshots; the HUD credits text 0x3e6dc0 groups too [H]).
std::u32string hud_thousands(int64_t v);
// #47 t08: the Text_Credits_tactical text. 0x3e6dc0 [H] writes "$ " and the grouped number into Text_Credits (component 6)
// and Text_Credits_tactical (0xf2); every HUD frame (0x2ee40, 0x3c990) passes (int) of the local player's credits, so the text
// jumps to a new amount (no tick: Credits_Scroll_Rate is the end credits roll's). '$' is the fonts' credits sign.
std::u32string hud_credits_text(int64_t credits);
// The Text_Tactical_Tech text: TEXT_TECH, a space and the level ("Tech 3"); "" for tech < 0. `text` null: the key itself.
// Shared by the Classic and Modern HUD styles (#584).
std::u32string hud_tech_text(const TextDatabase* text, int32_t tech);
// The Text_Planetary_Pop text: "used/cap", or "used" with no cap (cap < 0); "" for used < 0. Shared as hud_tech_text.
std::u32string hud_pop_text(int32_t used, int32_t cap);
// [port: Gabriel] #597 (docs/DECISIONS.md "Fleet cohesion"): the narrowest the Classic fleet cohesion line is squeezed
// into the planet name's Max_Text_Width, as a share of its natural width (a wider line overhangs the width, centred).
inline constexpr float kCohesionMinWidthScale = 0.85f;
// Reinforce event (legacy docs/re/COMMANDS.md §2 id 12) for a reinforcement unit dropped at a world point.
commands::EventData reinforce_event(uint32_t unit, Vec3 point);
// SetUnitAbilityMode (id 43) toggling `ability` on `units` (max 127).
commands::EventData ability_mode_event(std::span<const uint32_t> units, uint8_t ability, bool on);
// SetAbilityAutofire (id 58).
commands::EventData ability_autofire_event(std::span<const uint32_t> units, uint8_t ability, bool on);
// A click on an ability button (component `component`, named `name`): right -> SetAbilityAutofire toggled; left on a
// targeted ability -> HudAction::BeginAbilityTargeting (`queued` = the queue modifier); left otherwise ->
// SetUnitAbilityMode toggled.
void ability_button_click(const HudAbilityButton& a, int32_t component, const std::string& name, Button button, bool queued,
                          std::vector<commands::EventData>& out, std::vector<HudAction>* actions);
// [port] #587 the production and reinforcement clicks both HUD styles send (component `component`, named `name`):
// a queue entry's left click -> TacticalCancel; a buyable option's left click -> TacticalBuy at `builder`; an available
// reinforcement entry's left click -> BeginReinforcement.
HudAction tactical_cancel_action(int32_t component, const std::string& name, const HudQueueEntry& e);
// #588 a fleet tab's right click: HudAction::FleetGroupRename of group `id` (`name` UTF-8); Merge / Split: the fleet
// group command (Merge; Split = Create of the synced selection).
HudAction fleet_rename_action(int32_t component, uint16_t id, const std::string& name);
// [port] #591 the radar rules both HUD styles use: whether `u` has a blip (TacticalHud::build_radar's filter 0x35b4d0:
// Is_Visible_On_Radar / _Enemy_Radar, jammed / stealthed hostiles, Lua-hidden, contained and fogged units); a left press or
// drag at world (x, y) -> RadarCameraJump there; a right click with a selection -> the move there plus the RadarPing.
bool radar_blip_shown(const HudUnit& u, const HudTypeInfo* t, bool hostile);
// Whether `owner` is hostile to the local player: the host's rule (WorldHudContext::hostile), else any other owner.
inline bool hud_hostile(const WorldHudContext& w, int32_t owner) {
    return w.hostile ? w.hostile(owner) : (owner >= 0 && owner != w.local_player);
}
HudAction radar_jump_action(int32_t component, Vec2f world);
void radar_move_click(Vec2f world, float ground_z, std::vector<commands::EventData>& out, std::vector<HudAction>* actions);
commands::EventData fleet_button_event(bool merge);
// #592 a buy option's line both Modern placements show: its cost, or its blocked reason (Build_NN Tooltip_Text[0..2] for
// QueueFull / PopCap / Funds, port strings for the others).
std::u32string build_option_text(const HudBuildOption& o, const TextDatabase* text);
HudAction tactical_buy_action(int32_t component, const std::string& name, uint32_t builder, const HudBuildOption& o, bool shift,
                              bool ctrl);
HudAction begin_reinforcement_action(int32_t component, const std::string& name, const HudReinforcement& r);
// #645: the right click on a reinforcement pad (fleet groups on): HudAction::ReinforcementGroup for `r`.
HudAction reinforcement_group_action(int32_t component, const std::string& name, const HudReinforcement& r);
// [port] #587 a command-bar button's state under the Lua GUI_Component_Visibility / _Enable overrides (mods; ButtonRuntime
// script_visible / script_enabled, -1 = none): the override when the script set one, else the feed's.
inline bool script_hidden(int8_t script_visible, bool feed_hidden) { return script_visible >= 0 ? script_visible == 0 : feed_hidden; }
inline bool script_disabled(int8_t script_enabled, bool feed_disabled) {
    return script_enabled >= 0 ? script_enabled == 0 : feed_disabled;
}
// The tactical production / reinforcement slots of the command bar (TacticalHud::init, shared with the Modern deck).
inline constexpr int kTacticalBuyButtons = 6, kTacticalQueueSlots = 20, kReinforceRows = 5, kReinforceColumns = 4;

}  // namespace alamo::ui
