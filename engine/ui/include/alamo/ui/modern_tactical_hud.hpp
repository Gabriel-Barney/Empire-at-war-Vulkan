// [port: Gabriel] The Modern tactical HUD style (#520, first slice #584; docs/DECISIONS.md "Modern HUD: exception to the
// current focus"). Retail has one HUD, kept as the Classic style (TacticalHud). Modern is a second view over the same
// feed: it takes the TacticalHudFrame the host already builds and sends the same commands and HUD actions as Classic for
// the same intent, so the simulation, replays and saves cannot tell which style the player used.
//
// The host owns the style (hud_style.hpp, profile key hud_style): for each frame it routes input to and draws one style.
// The world-space layer (brackets, bars, waypoints, reticles, selection circles) stays the Classic HUD's
// (TacticalHud::set_world_split); Modern draws over it. This slice draws:
//   - the status corner (top left): credits, population and tech, the same texts as Classic (hud_credits_text,
//     hud_pop_text, hud_tech_text);
//   - the game controls (top right): menu (b_option_t), pause (b_play_pause_t) and fast forward (b_fast_forward_t), whose
//     clicks are the Classic components' clicks (same component ids, same commands and actions), and the HUD style switch
//     (HudAction::HudStyleToggle);
//   - the command deck along the bottom (#585): with own units selected, the selection as unit cards (icon, count, name,
//     a solid hull bar and a striped shield bar; the "+N" overflow card), the wing buttons and a clear-selection button
//     in the selection header, and the six order buttons (the Classic c_button00..05 ids and rule, order_button_enabled)
//     with their live hotkey badges, greyed with a dashed outline when no selected unit can do the order, and the fleet
//     cohesion line. Cards group by the Classic functions (group_selection / group_selection_fleets) and click as the
//     Classic portraits do (plain: the group; Shift: drop it; Ctrl: its first unit);
//   - the alert strip (top centre, #589): the newest of TacticalHudFrame::alerts (advisor lines, radar events, production
//     done, objectives added; the host keeps them in a HudAlertLog) with a count of the others and, for an alert with a
//     position, Jump (its live hotkey badge, slot AlertJump) -> HudAction::RadarCameraJump there;
//   - the objectives panel (top right, under the controls, #589): the Classic objectives (hud_objective_text) with their
//     state as a shape (open: an outlined square, done: a filled square, failed: a cross); folded to its header while
//     objectives_hidden (the Mission Holocron key, Tab, as Classic);
//   - while paused, the Game Paused banner with Resume Game (the Classic banner's HudAction::ResumeGame on its
//     attack_button component): the Classic banner is drawn into the Classic panels, which Modern replaces.
// Sizes: panels are in canvas units, so the UI scale (HudCanvas) sizes them as it sizes Classic. Text has its own scale
// (set_text_scale, 100..200 %, default 115 %), at which all Modern text is 18 px at 1080p (Xbox accessibility guideline 101).
#pragma once
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "alamo/commands/command.hpp"
#include "alamo/ui/hotkeys.hpp"
#include "alamo/ui/hud_canvas.hpp"
#include "alamo/ui/hud_components.hpp"
#include "alamo/ui/hud_draw.hpp"
#include "alamo/ui/hud_input.hpp"
#include "alamo/ui/hud_radar.hpp"
#include "alamo/ui/hud_style.hpp"
#include "alamo/ui/modern_hud_view.hpp"
#include "alamo/ui/tactical_hud.hpp"

namespace alamo::ui {

// Hit-box ids of the tactical Modern-only controls (kModernHitBase: modern_hud_view.hpp).
inline constexpr int32_t kModernClearHit = kModernHitBase + 2;        // the deck's clear-selection button
inline constexpr int32_t kModernCardHitBase = kModernHitBase + 0x100;  // + card index (the deck's selection cards)
inline constexpr int32_t kModernStationHit = kModernHitBase + 4;          // #587 the shortcut that selects the station
inline constexpr int32_t kModernMapHit = kModernHitBase + 5;              // #591 the map, when the table has no "radar"
inline constexpr int32_t kModernFleetShortcutBase = kModernHitBase + 0x200;  // + fleet group index (#587 shortcuts)
inline constexpr int32_t kModernDisciplineHit = kModernHitBase + 6;  // #599 the fleet discipline control
inline constexpr int32_t kModernNewGroupHit = kModernHitBase + 7;    // #599 New group (the N key's Create)
inline constexpr int32_t kModernLockHit = kModernHitBase + 8;        // #599 Lock (the Y key's SetLock)
inline constexpr int32_t kModernWingTabBase = kModernHitBase + 0x800;  // #600 + wing index: a wing run's tab (WingSelect)
// Hull bar colours by health (badly damaged units stand out): above kModernHullWarn, above kModernHullCritical, below.
inline constexpr float kModernHullWarn = 0.6f, kModernHullCritical = 0.3f;

// The alert text of a radar event (#589), nullopt for none. An allow-list: only the base-under-attack ping (an unnamed
// marker) -> "Base under attack". Story blips (named markers), the player's own clicks and beacons, and every other
// RadarMap.xml event -> none (the host announces capture points itself, at the transition, with who took the point).
std::optional<std::u32string> radar_alert_text(const RadarEvent& e, const TextDatabase* text);

struct ModernHudStats {
    bool status_corner = false;          // the status corner drawn
    std::u32string credits_text, pop_text, tech_text;  // its values ("" = that row not drawn)
    uint32_t controls = 0;               // game control buttons drawn (menu, pause, fast forward)
    bool style_switch = false;           // the HUD style switch drawn
    bool deck = false;                   // the command deck drawn
    uint32_t cards = 0;                  // selection cards drawn (the "+N" card included)
    bool overflow_card = false;          // the last card is the "+N" overflow
    uint32_t orders = 0, orders_live = 0;  // order buttons drawn, enabled
    uint32_t wings = 0;                  // wing buttons drawn
    bool clear_button = false;
    bool discipline_drawn = false;                // #599 the fleet discipline control (it shows the fleet cohesion)
    uint32_t discipline_lights = 0;               // its steps (2: Loose / Tight; 3 with Locked)
    FleetDiscipline discipline = FleetDiscipline::Loose;  // the step lit
    bool discipline_hint = false;                 // the one-line-per-step hint (hovered)
    uint32_t fleet_buttons = 0;                   // New group and Lock drawn
    uint32_t key_badges = 0;             // hotkey badges drawn
    std::u32string alert_text;           // the alert strip's alert ("" = no strip)
    uint32_t alerts_more = 0;            // other alerts behind it
    bool jump = false;                   // its Jump button drawn
    // #587 the deck's sections: battle actions (retreat, reinforcements, special weapon / bombing run, beacon), always;
    // production (the builder card, its options and queue) while a builder is selected; the reinforcement list while open;
    // with nothing selected, the shortcuts (wings, fleet groups, the station) in a deck that shrinks to them (contextual on).
    uint32_t actions = 0, actions_live = 0;  // battle action buttons drawn / enabled
    bool production = false;                 // the production section drawn
    uint32_t build_options = 0, build_blocked = 0, queue_entries = 0, reinforcements = 0;
    std::vector<std::u32string> build_texts;  // each option's line: its cost, or its blocked reason
    uint32_t shortcuts = 0;                  // wing, fleet group and station shortcuts drawn
    float deck_width = 0.0f;                 // canvas units
    uint32_t fleet_tabs = 0, fleet_merge = 0, fleet_split = 0;  // #588 group tabs and their Merge / Split buttons drawn
    // #600 wing run tabs; the tabs' state chips (a padlock LOCKED, an open padlock FREE, a wing WING) and health bars
    uint32_t wing_tabs = 0, chips_locked = 0, chips_free = 0, chips_wing = 0, tab_health_bars = 0;
    // #591 the map (bottom left above the deck, or right with the map side option): blips by shape - own units squares,
    // hostile diamonds, neutral and allied circles - in their team colours; pings; the camera footprint. The hover target
    // card: the hovered hostile unit's name, hull, shields and hard points beside it (no hit box: clicks pass through).
    bool map = false, map_right = false;
    uint32_t map_squares = 0, map_diamonds = 0, map_circles = 0, map_pings = 0;
    uint32_t map_drop_zones = 0, map_block_zones = 0;  // #592 land reinforcement zones marked (enabling / prevention)
    bool pad_options = false;                          // #592 the buy options drawn at the build pad (land, an anchor)
    bool target_card = false;
    uint32_t target_id = 0, target_hardpoints = 0;
    uint32_t abilities = 0;              // #586 ability buttons drawn
    uint32_t ability_rings = 0;          // of them recharging (a ring and the seconds left)
    uint32_t ability_auto = 0;           // of them with the AUTO badge (autofire on)
    uint32_t ability_active = 0;         // of them in the active look
    std::vector<std::u32string> ability_counts;  // the rings' counts ("12", or "40%" without seconds), button order
    bool objectives_panel = false, objectives_folded = false;
    std::vector<std::u32string> objective_texts;  // the panel's entries (unfolded), feed order
    bool pause_banner = false;           // the Game Paused banner drawn
    uint32_t hit_boxes = 0, quads = 0;
};

class ModernTacticalHud : public ModernHudView {
public:
    // `table` (the CommandBarComponents the Classic HUD uses) must outlive the HUD; it names the shared controls' ids.
    // `text` may be null (port strings in English, retail keys as the keys).
    void init(const ComponentTable& table, const TextureAtlas& atlas, FontCache& fonts, const TextDatabase* text = nullptr);
    // #587 the Classic HUD's button states (TacticalHud::buttons), for the Lua GUI_Component_Visibility / _Enable overrides
    // on the shared battle buttons (null: none). Must outlive the HUD.
    void set_script_state(const HudButtons* classic) { script_ = classic; }

    // The TacticalHud::build interface; the selection (`units`, `types`) fills the deck from #585 on.
    void build(const TacticalHudFrame& frame, std::span<const HudUnit> units, std::span<const HudTypeInfo> types);
    const ModernHudStats& stats() const { return stats_; }
    // The cards of the last build (group_selection / group_selection_fleets order; the "+N" entry last).
    const std::vector<SelectedGroup>& selection_cards() const { return groups_; }
    // c_button00..05 enabled after the previous build (order_button_enabled on the frame's HudOrderButtons); false when the
    // deck drew no orders (no own selection) or the component is missing.
    bool order_button_live(uint32_t button) const { return button < kOrderButtonCount && order_live_[button]; }

    // One frame of HUD input, on the hit boxes of the previous build(): clicks -> the commands and actions Classic sends
    // for the same control (TacticalHud::update_input), plus HudStyleToggle for the style switch.
    void update_input(const RoutedInput& routed, bool shift, bool ctrl, std::vector<commands::EventData>& out,
                      std::vector<HudAction>* actions = nullptr);
    // #586 an ability hotkey, as TacticalHud::activate_ability: a left click on the drawn ability button showing
    // `ability_type` (the same command / BeginAbilityTargeting action). False when none is drawn or it is disabled.
    bool activate_ability(uint8_t ability_type, bool queued, std::vector<commands::EventData>& out,
                          std::vector<HudAction>* actions = nullptr) const;
    // #591 the map's ping models (TacticalHud::radar_models: an event type's Event_Model_Name) for the host's model drawer,
    // when the frame says the host draws them (radar_event_models).
    const std::vector<HudModelDraw>& radar_models() const { return radar_models_; }
    // The drawn button of `ability_type` (canvas centre and size) after the previous build, for the story's ability arrow
    // and flash (TacticalHud::ability_component's Modern counterpart). False when it is not drawn.
    bool ability_box(uint8_t ability_type, Vec2f& center, Vec2f& size) const;

private:
    Rgba8 hull_colour(float health) const;
    void build_status(const TacticalHudFrame& f);
    void build_controls(const TacticalHudFrame& f);
    void build_deck(const TacticalHudFrame& f, std::span<const HudUnit> units, std::span<const HudTypeInfo> types);
    void build_selection(const TacticalHudFrame& f, std::span<const HudUnit> units, std::span<const HudTypeInfo> types,
                         Vec2f top_left, float width, float height);
    float build_orders(const TacticalHudFrame& f, Vec2f top_right, float height);  // returns the width used
    float build_discipline(const TacticalHudFrame& f, std::span<const HudUnit> units, Vec2f top_right, float height);
    FleetDisciplineStep discipline_{};  // the last build's step (its click), drawn: discipline_drawn_
    bool discipline_drawn_ = false;
    bool discipline_selection_ = false;  // the control's selection (selected own units) is not empty: New group / Lock act
    float build_abilities(const TacticalHudFrame& f, std::span<const HudUnit> units, Vec2f top_right, float height, float max_width);
    void build_map(const TacticalHudFrame& f, std::span<const HudUnit> units, std::span<const HudTypeInfo> types);
    void build_target_card(const TacticalHudFrame& f, std::span<const HudUnit> units, std::span<const HudTypeInfo> types);
    int32_t map_id() const { return radar_ >= 0 ? radar_ : kModernMapHit; }
    float map_side() const { return std::min(0.3f * canvas_.height, 0.22f * canvas_.width); }
    std::vector<HudModelDraw> radar_models_;
    int32_t radar_ = -1;      // the Classic "radar" component: the map's hit id (its RadarCameraJump component)
    RadarView map_view_;      // the last build's map rectangle (to_world for clicks)
    bool map_drag_ = false;   // a left press on the map (camera jumps while held)
    bool select_groups_ = false;  // the selection has groups (Classic's radar move click needs one)
    float ground_z_ = 0.0f;
    float build_actions(const TacticalHudFrame& f, Vec2f top_left, float height, bool draw);  // returns the width
    float build_shortcuts(const TacticalHudFrame& f, std::span<const HudUnit> units, std::span<const HudTypeInfo> types,
                          Vec2f top_left, float height, bool draw);
    float build_queue(const TacticalHudFrame& f, Vec2f top_left, float height, float max_width, bool builder_only, bool draw);
    Tile option_tile(size_t i, const HudBuildOption& o, Vec2f c, Vec2f size) const;
    void add_option(const Tile& t, size_t i, const HudBuildOption& o);
    void build_pad_options(const TacticalHudFrame& f);
    Vec2f grid2_cell(size_t i, Vec2f top_left, Vec2f cell, float gap) const;  // two rows, filled by columns
    const HudButtons* script_ = nullptr;
    float build_production(const TacticalHudFrame& f, std::span<const HudUnit> units, std::span<const HudTypeInfo> types,
                           Vec2f top_left, float height, float max_width);
    float build_reinforcements(const TacticalHudFrame& f, Vec2f top_left, float height, float max_width);
    std::vector<uint32_t> selection_ids() const;
    void build_pause(const TacticalHudFrame& f);
    void build_objectives(const TacticalHudFrame& f, float top);
    float controls_bottom_ = 0.0f;  // canvas y under the controls row (the objectives panel hangs from it)

    int32_t option_ = -1, pause_ = -1, fast_forward_ = -1;  // b_option_t, b_play_pause_t, b_fast_forward_t
    int32_t resume_ = -1;                                   // attack_button: the pause banner's Resume Game
    int32_t order_[kOrderButtonCount] = {-1, -1, -1, -1, -1, -1};  // c_button00..05
    bool order_live_[kOrderButtonCount] = {};
    std::vector<int32_t> special_button_;  // special_button_NN: the ability buttons' Classic ids (hit ids and action names)
    size_t panel_slots_ = 0;               // s_select_NN present: Classic's fleet grouping bound
    struct AbilityButton : HudAbilityButton {
        int32_t component = -1;  // special_button_NN (-1: the table has none; the hotkey still fires it, as Classic's)
        bool drawn = false;
        Vec2f center{}, size{};
    };
    std::vector<AbilityButton> abilities_;  // every button Classic would build (its 48 cap), deck order; the first drawn
    // #587 the shared battle controls (Classic's ids) and this build's slots.
    int32_t retreat_ = -1, special_weapon_ = -1, reinforce_button_ = -1, beacon_ = -1;
    std::vector<int32_t> buy_, queue_, reinforce_;  // Build_NN, tqueueNN, r_RRCC
    std::vector<std::pair<int32_t, HudBuildOption>> buy_slots_;
    std::vector<std::pair<int32_t, HudQueueEntry>> queue_slots_;
    std::vector<std::pair<int32_t, HudReinforcement>> reinforce_slots_;
    std::vector<std::vector<uint32_t>> fleet_shortcuts_;
    // #588 per drawn group tab (Classic's tab ids: kFleetTabHitBase / kFleetMergeHitBase / kFleetSplitHitBase + index).
    std::vector<std::vector<uint32_t>> fleet_tab_ids_;
    std::vector<std::pair<uint16_t, std::string>> fleet_tab_group_;  // the group id and name (UTF-8), for Rename
    struct CardRow {  // the selection cards' geometry
        Vec2f first{};    // the first card's centre
        Vec2f card{};     // a card's size
        float gap = 0.0f, right = 0.0f;  // between cards; the section's right edge
    };
    void build_fleet_tabs(const TacticalHudFrame& f, std::span<const HudUnit> units, const CardRow& row, float tab_h);
    uint32_t builder_ = commands::kNoObject, station_ = commands::kNoObject;
    std::vector<SelectedGroup> groups_;
    std::vector<std::vector<uint32_t>> wing_ids_;  // per TacticalHudFrame::wings entry (empty: not drawn), as Classic's
    Rgba8 credits_color_{255, 212, 33, 255};                // Text_Credits_tactical's Text_Color when the data has one
    bool paused_ = false;
    ModernHudStats stats_;
};

}  // namespace alamo::ui
