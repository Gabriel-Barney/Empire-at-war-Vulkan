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
// The alert text of a radar event (#589), nullopt for none: a base-under-attack ping (an unnamed marker) -> "Base under
// attack"; a story blip (a named marker) and the player's own clicks (types with "Click") -> none; any other event its
// RadarMap.xml type name with '_' as spaces [port: no display text in the data].
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
    void resize(uint32_t width, uint32_t height);
    // The UI scale (as TacticalHud::set_ui_scale): takes effect on the next resize().
    void set_ui_scale(float s) { canvas_.ui_scale = s > 0.0f ? s : 0.0f; }
    // Modern text scale, clamped to kModernTextScaleMin .. kModernTextScaleMax (the #590 option sets it).
    void set_text_scale(float s);
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

private:
    int32_t font() const;
    float text_height_units() const;
    Rgba8 accent(uint8_t alpha) const;
    void build_status(const TacticalHudFrame& f);
    void build_controls(const TacticalHudFrame& f);
    void build_deck(const TacticalHudFrame& f, std::span<const HudUnit> units, std::span<const HudTypeInfo> types);
    void build_selection(const TacticalHudFrame& f, std::span<const HudUnit> units, std::span<const HudTypeInfo> types,
                         Vec2f top_left, float width, float height);
    float build_orders(const TacticalHudFrame& f, Vec2f top_right, float height);  // returns the width used
    void key_badge(uint16_t slot, Vec2f top_right, uint32_t layer);
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
