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
#include "alamo/ui/hud_style.hpp"
#include "alamo/ui/tactical_hud.hpp"

namespace alamo::ui {

class FontCache;

// Modern text scale (HUD option hud_text_scale, #590): 1 = 100 %.
inline constexpr float kModernTextScaleMin = 1.0f, kModernTextScaleMax = 2.0f, kModernTextScaleDefault = 1.15f;
// Text height in pixels at 1080p and 100 % text scale: every Modern text, labels too, is 18 px at the default 115 %.
inline constexpr float kModernTextPx = 18.0f / kModernTextScaleDefault;

// Hit-box ids of the Modern-only controls (the controls shared with Classic use the Classic component ids).
inline constexpr int32_t kModernHitBase = 0x44000000;
inline constexpr int32_t kModernStyleSwitchHit = kModernHitBase + 0;  // the HUD style switch
inline constexpr int32_t kModernPanelHit = kModernHitBase + 1;        // a panel's back: takes the click, does nothing
inline constexpr int32_t kModernClearHit = kModernHitBase + 2;        // the deck's clear-selection button
inline constexpr int32_t kModernJumpHit = kModernHitBase + 3;         // the alert strip's Jump
inline constexpr int32_t kModernCardHitBase = kModernHitBase + 0x100;  // + card index (the deck's selection cards)
inline constexpr int32_t kModernStationHit = kModernHitBase + 4;          // #587 the shortcut that selects the station
inline constexpr int32_t kModernFleetShortcutBase = kModernHitBase + 0x200;  // + fleet group index (#587 shortcuts)
// Hull bar colours by health (badly damaged units stand out): above kModernHullWarn, above kModernHullCritical, below.
inline constexpr float kModernHullWarn = 0.6f, kModernHullCritical = 0.3f;

// [port: Gabriel] #589 The host's alert list for the Modern alert strip: push() what happened, update() each frame
// (ages the entries, drops those older than kSeconds and all but the newest kMax).
class HudAlertLog {
public:
    static constexpr double kSeconds = 12.0;
    static constexpr size_t kMax = 8;
    void push(HudAlert::Kind kind, std::u32string text, double now);
    void push_at(HudAlert::Kind kind, std::u32string text, float x, float y, double now);
    void update(double now);
    void clear() { items_.clear(); }
    std::span<const HudAlert> items() const { return items_; }  // oldest first

private:
    std::vector<HudAlert> items_;
};
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
    bool clear_button = false, cohesion = false;
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

class ModernTacticalHud {
public:
    // `table` (the CommandBarComponents the Classic HUD uses) must outlive the HUD; it names the shared controls' ids.
    // `text` may be null (port strings in English, retail keys as the keys).
    void init(const ComponentTable& table, const TextureAtlas& atlas, FontCache& fonts, const TextDatabase* text = nullptr);
    // The live bindings the key badges show (null: no badges). Must outlive the HUD.
    void set_hotkeys(const HotkeyTable* keys) { keys_ = keys; }
    // #587 the Classic HUD's button states (TacticalHud::buttons), for the Lua GUI_Component_Visibility / _Enable overrides
    // on the shared battle buttons (null: none). Must outlive the HUD.
    void set_script_state(const HudButtons* classic) { script_ = classic; }
    void resize(uint32_t width, uint32_t height);
    // The UI scale (as TacticalHud::set_ui_scale): takes effect on the next resize().
    void set_ui_scale(float s) { canvas_.ui_scale = s > 0.0f ? s : 0.0f; }
    // Modern text scale, clamped to kModernTextScaleMin .. kModernTextScaleMax (also set by set_options).
    void set_text_scale(float s);
    // [port] #590 the Interface page's Modern options (clamped): text size, colours, key hints, order labels, panel opacity;
    // the contextual deck (#587); the map side is kept for the map (#591). Takes effect on the next build().
    void set_options(const ModernHudOptions& o);
    const ModernHudOptions& options() const { return options_; }
    float text_scale() const { return text_scale_; }
    const HudCanvas& canvas() const { return canvas_; }

    // The TacticalHud::build interface; the selection (`units`, `types`) fills the deck from #585 on.
    void build(const TacticalHudFrame& frame, std::span<const HudUnit> units, std::span<const HudTypeInfo> types);
    const HudDrawList& draw_list() const { return dl_; }
    const ModernHudStats& stats() const { return stats_; }
    // The cards of the last build (group_selection / group_selection_fleets order; the "+N" entry last).
    const std::vector<SelectedGroup>& selection_cards() const { return groups_; }
    // c_button00..05 enabled after the previous build (order_button_enabled on the frame's HudOrderButtons); false when the
    // deck drew no orders (no own selection) or the component is missing.
    bool order_button_live(uint32_t button) const { return button < kOrderButtonCount && order_live_[button]; }

    // Topmost hit box under a pixel (-1: none): a Classic component id for the shared controls, kModern* otherwise.
    int32_t hit_test(int32_t px, int32_t py) const;
    const std::vector<HudHitBox>& hit_boxes() const { return hits_; }

    // One frame of HUD input, on the hit boxes of the previous build(): clicks -> the commands and actions Classic sends
    // for the same control (TacticalHud::update_input), plus HudStyleToggle for the style switch.
    void update_input(const RoutedInput& routed, bool shift, bool ctrl, std::vector<commands::EventData>& out,
                      std::vector<HudAction>* actions = nullptr);
    // The AlertJump hotkey: the Jump of the strip's alert of the previous build (RadarCameraJump to its position). False
    // when the strip shows no alert with a position (nothing emitted).
    bool jump(std::vector<HudAction>& actions) const;
    // #586 an ability hotkey, as TacticalHud::activate_ability: a left click on the drawn ability button showing
    // `ability_type` (the same command / BeginAbilityTargeting action). False when none is drawn or it is disabled.
    bool activate_ability(uint8_t ability_type, bool queued, std::vector<commands::EventData>& out,
                          std::vector<HudAction>* actions = nullptr) const;
    // The drawn button of `ability_type` (canvas centre and size) after the previous build, for the story's ability arrow
    // and flash (TacticalHud::ability_component's Modern counterpart). False when it is not drawn.
    bool ability_box(uint8_t ability_type, Vec2f& center, Vec2f& size) const;

private:
    int32_t font() const;
    float text_height_units() const;
    Rgba8 accent(uint8_t alpha) const;
    Rgba8 hull_colour(float health) const;
    struct Palette {  // set_options: the chosen colours (HudColours) at the panel opacity
        Rgba8 back{10, 14, 22, 204}, button{24, 32, 46, 234}, text{236, 240, 244, 255}, label{170, 182, 198, 255};
        Rgba8 disabled{120, 128, 140, 255}, shield{90, 190, 255, 255};
        Rgba8 hull_ok{96, 210, 110, 255}, hull_warn{236, 188, 60, 255}, hull_bad{230, 70, 60, 255};
        Rgba8 accent{};
        bool faction_accent = true;  // the local faction's colour (kAccent) instead of `accent`
    };
    Palette colours_;
    ModernHudOptions options_;
    void build_status(const TacticalHudFrame& f);
    void build_controls(const TacticalHudFrame& f);
    void build_deck(const TacticalHudFrame& f, std::span<const HudUnit> units, std::span<const HudTypeInfo> types);
    void build_selection(const TacticalHudFrame& f, std::span<const HudUnit> units, std::span<const HudTypeInfo> types,
                         Vec2f top_left, float width, float height);
    float build_orders(const TacticalHudFrame& f, Vec2f top_right, float height);  // returns the width used
    float build_abilities(const TacticalHudFrame& f, std::span<const HudUnit> units, Vec2f top_right, float height, float max_width);
    // #587
    struct Tile {
        int32_t id = -1;
        Vec2f c{}, size{};
        bool enabled = true, selected = false;
        std::string icon;
        std::u32string text;    // centred (under the icon, if any)
        float ring = -1.0f;     // 0..1 countdown ring, < 0 none
        std::u32string badge;   // a count in the top-left corner
        int32_t hotkey = -1;    // key badge slot
    };
    void tile(const Tile& t);
    float build_actions(const TacticalHudFrame& f, Vec2f top_left, float height, bool draw);  // returns the width
    float build_shortcuts(const TacticalHudFrame& f, std::span<const HudUnit> units, std::span<const HudTypeInfo> types,
                          Vec2f top_left, float height, bool draw);
    float build_queue(const TacticalHudFrame& f, Vec2f top_left, float height, float max_width, bool builder_only, bool draw);
    Vec2f grid2_cell(size_t i, Vec2f top_left, Vec2f cell, float gap) const;  // two rows, filled by columns
    const HudButtons* script_ = nullptr;
    float build_production(const TacticalHudFrame& f, std::span<const HudUnit> units, std::span<const HudTypeInfo> types,
                           Vec2f top_left, float height, float max_width);
    float build_reinforcements(const TacticalHudFrame& f, Vec2f top_left, float height, float max_width);
    void ability_ring(Vec2f c, float radius, float recharge);
    void key_badge(uint16_t slot, Vec2f top_right, uint32_t layer, bool always = false);  // always: even with key hints off
    std::vector<uint32_t> selection_ids() const;
    void build_pause(const TacticalHudFrame& f);
    void build_alerts(const TacticalHudFrame& f);
    void build_objectives(const TacticalHudFrame& f, float top);
    float controls_bottom_ = 0.0f;  // canvas y under the controls row (the objectives panel hangs from it)
    bool jump_ok_ = false;          // the strip's alert has a position
    Vec2f jump_at_{};
    void hit(int32_t id, uint32_t layer, Vec2f center, Vec2f size);

    const ComponentTable* table_ = nullptr;
    const TextureAtlas* atlas_ = nullptr;
    const HotkeyTable* keys_ = nullptr;
    FontCache* fonts_ = nullptr;
    const TextDatabase* text_ = nullptr;
    HudCanvas canvas_ = HudCanvas::make(1024, 768);
    float text_scale_ = kModernTextScaleDefault;
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
    std::vector<int32_t> dead_actions_;             // battle actions drawn disabled (their clicks are swallowed)
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
    uint8_t faction_alt_ = 1;
    bool paused_ = false;
    HudDrawList dl_;
    std::vector<HudHitBox> hits_;
    int32_t hover_ = -1;
    ModernHudStats stats_;
};

}  // namespace alamo::ui
