// [port: Gabriel] The Modern tactical HUD style (#520, first slice #584; docs/DECISIONS.md "Modern HUD: exception to the
// current focus"). Retail has one HUD, kept as the Classic style (TacticalHud, unchanged). Modern is a second view over the
// same feed: it takes the TacticalHudFrame the host already builds and sends the same commands and HUD actions as Classic
// for the same intent, so the simulation, replays and saves cannot tell which style the player used.
//
// The host owns the style (HudStyle, profile key hud_style): for each frame it builds, routes input to and draws one
// style. The world-space layer (brackets, bars, waypoints, reticles, selection circles) stays the Classic HUD's
// (TacticalHud::set_world_split); Modern draws over it. This slice draws:
//   - the status corner (top left): credits, population and tech, the same texts as Classic (hud_credits_text,
//     hud_pop_text, hud_tech_text);
//   - the game controls (top right): menu (b_option_t), pause (b_play_pause_t) and fast forward (b_fast_forward_t), whose
//     clicks are the Classic components' clicks (same component ids, same commands and actions), and the HUD style switch
//     (HudAction::HudStyleToggle);
//   - the command deck along the bottom, empty for now (#585 fills it).
// Sizes: panels are in canvas units, so the UI scale (HudCanvas) sizes them as it sizes Classic. Text has its own scale
// (set_text_scale, 100..200 %, default 115 %), at which body text is 18 px at 1080p (Xbox accessibility guideline 101).
#pragma once
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "alamo/commands/command.hpp"
#include "alamo/ui/hud_canvas.hpp"
#include "alamo/ui/hud_components.hpp"
#include "alamo/ui/hud_draw.hpp"
#include "alamo/ui/hud_input.hpp"
#include "alamo/ui/tactical_hud.hpp"

namespace alamo::ui {

class FontCache;

// The player's HUD style. Classic is the default; scripted, smoke and benchmark runs always use Classic.
enum class HudStyle : uint8_t { Classic, Modern };
// Profile names: "classic", "modern".
std::string_view hud_style_name(HudStyle s);
// A profile value (case-insensitive); nullopt for anything else.
std::optional<HudStyle> parse_hud_style(std::string_view s);
inline HudStyle other_hud_style(HudStyle s) { return s == HudStyle::Modern ? HudStyle::Classic : HudStyle::Modern; }

// Modern text scale (HUD option hud_text_scale): 1 = 100 %.
inline constexpr float kModernTextScaleMin = 1.0f, kModernTextScaleMax = 2.0f, kModernTextScaleDefault = 1.15f;
// Text heights in pixels at 1080p and 100 % text scale: body text reaches 18 px at the default 115 %; labels never go
// below 14 px.
inline constexpr float kModernBodyPx = 18.0f / kModernTextScaleDefault, kModernLabelPx = 14.0f;

// Hit-box ids of the Modern-only controls (the controls shared with Classic use the Classic component ids).
inline constexpr int32_t kModernHitBase = 0x44000000;
inline constexpr int32_t kModernStyleSwitchHit = kModernHitBase + 0;  // the HUD style switch
inline constexpr int32_t kModernPanelHit = kModernHitBase + 1;        // a panel's back: takes the click, does nothing

struct ModernHudStats {
    bool status_corner = false;          // the status corner drawn
    std::u32string credits_text, pop_text, tech_text;  // its values ("" = that row not drawn)
    uint32_t controls = 0;               // game control buttons drawn (menu, pause, fast forward)
    bool style_switch = false;           // the HUD style switch drawn
    bool deck = false;                   // the command deck drawn
    uint32_t hit_boxes = 0, quads = 0;
};

class ModernTacticalHud {
public:
    // `table` (the CommandBarComponents the Classic HUD uses) must outlive the HUD; it names the shared controls' ids.
    // `text` may be null (keys shown as text, port strings in English).
    void init(const ComponentTable& table, FontCache& fonts, const TextDatabase* text = nullptr);
    void resize(uint32_t width, uint32_t height);
    // The UI scale (as TacticalHud::set_ui_scale): takes effect on the next resize().
    void set_ui_scale(float s) { canvas_.ui_scale = s > 0.0f ? s : 0.0f; }
    // Modern text scale, clamped to kModernTextScaleMin .. kModernTextScaleMax.
    void set_text_scale(float s);
    float text_scale() const { return text_scale_; }
    const HudCanvas& canvas() const { return canvas_; }

    void build(const TacticalHudFrame& frame, std::span<const HudUnit> units, std::span<const HudTypeInfo> types);
    const HudDrawList& draw_list() const { return dl_; }
    const ModernHudStats& stats() const { return stats_; }

    // Topmost hit box under a pixel (-1: none): a Classic component id for the shared controls, kModern* otherwise.
    int32_t hit_test(int32_t px, int32_t py) const;
    const std::vector<HudHitBox>& hit_boxes() const { return hits_; }

    // One frame of HUD input, on the hit boxes of the previous build(): clicks -> the commands and actions Classic sends
    // for the same control (TacticalHud::update_input), plus HudStyleToggle for the style switch.
    void update_input(const RoutedInput& routed, bool shift, bool ctrl, double now, std::vector<commands::EventData>& out,
                      std::vector<HudAction>* actions = nullptr);

private:
    int32_t font(float px_1080) const;
    float text_px(float px_1080) const;
    void build_status(const TacticalHudFrame& f);
    void build_controls(const TacticalHudFrame& f);
    void build_deck(const TacticalHudFrame& f);
    void hit(int32_t id, uint32_t layer, Vec2f center, Vec2f size);

    const ComponentTable* table_ = nullptr;
    FontCache* fonts_ = nullptr;
    const TextDatabase* text_ = nullptr;
    HudCanvas canvas_ = HudCanvas::make(1024, 768);
    float text_scale_ = kModernTextScaleDefault;
    int32_t option_ = -1, pause_ = -1, fast_ = -1;  // b_option_t, b_play_pause_t, b_fast_forward_t
    bool paused_ = false;
    HudDrawList dl_;
    std::vector<HudHitBox> hits_;
    int32_t hover_ = -1;
    ModernHudStats stats_;
};

}  // namespace alamo::ui
