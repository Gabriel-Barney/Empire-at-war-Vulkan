// [port: Gabriel] What the Modern HUD views (#520) share: the look (the palette, text size and key badges of the
// Interface page's options, #590), the deck tiles, the top-row controls and the HUD style switch, and the alert strip
// with its Jump (#589). ModernTacticalHud (#584) and ModernGalacticHud (#593) derive from it; each builds its own panels
// over its own feed.
#pragma once
#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "alamo/ui/hotkeys.hpp"
#include "alamo/ui/hud_canvas.hpp"
#include "alamo/ui/hud_components.hpp"
#include "alamo/ui/hud_draw.hpp"
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
inline constexpr int32_t kModernJumpHit = kModernHitBase + 3;         // the alert strip's Jump

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

// The Modern look's colours (#590 colour choices), shared by the tactical and galactic Modern views: dark translucent
// panels at the panel opacity with an edge in the local faction's colour (kModernAccent); colour-blind safe = the
// Okabe-Ito palette (blue accent, bluish green / orange / vermillion hull); high contrast = near-opaque black panels,
// white text, yellow accent.
struct ModernPalette {
    Rgba8 back{10, 14, 22, 204}, button{24, 32, 46, 234}, text{236, 240, 244, 255}, label{170, 182, 198, 255};
    Rgba8 disabled{120, 128, 140, 255}, shield{90, 190, 255, 255};
    Rgba8 hull_ok{96, 210, 110, 255}, hull_warn{236, 188, 60, 255}, hull_bad{230, 70, 60, 255};
    Rgba8 accent{};
    bool faction_accent = true;  // the local faction's colour (kModernAccent) instead of `accent`
    Rgba8 edge(uint8_t faction_alt, uint8_t alpha) const;  // the accent at `alpha`
};
ModernPalette modern_palette(const ModernHudOptions& o);
inline constexpr Rgba8 kModernAccent[2] = {{226, 120, 48, 255}, {92, 156, 230, 255}};  // faction_alt 0 (Rebel), 1 (others)

// The alert strip of a build (ModernHudView::alert_strip): the shown alert's text ("" = no strip), the others behind it,
// whether its Jump is drawn.
struct ModernAlertStrip {
    std::u32string text;
    uint32_t more = 0;
    bool jump = false;
};

class ModernHudView {
public:
    // The live bindings the key badges show (null: no badges). Must outlive the HUD.
    void set_hotkeys(const HotkeyTable* keys) { keys_ = keys; }
    void resize(uint32_t width, uint32_t height);
    // The UI scale (as the Classic HUDs' set_ui_scale): takes effect on the next resize().
    void set_ui_scale(float s) { canvas_.ui_scale = s > 0.0f ? s : 0.0f; }
    // Modern text scale, clamped to kModernTextScaleMin .. kModernTextScaleMax (also set by set_options).
    void set_text_scale(float s);
    // [port] #590 the Interface page's Modern options (clamped): text size, colours, key hints, order labels, panel opacity;
    // the contextual deck (#587), the map side (#591). Takes effect on the next build().
    void set_options(const ModernHudOptions& o);
    const ModernHudOptions& options() const { return options_; }
    float text_scale() const { return text_scale_; }
    const HudCanvas& canvas() const { return canvas_; }
    const HudDrawList& draw_list() const { return dl_; }
    // Topmost hit box under a pixel (-1: none): a Classic component id for the shared controls, kModern* otherwise.
    int32_t hit_test(int32_t px, int32_t py) const;
    const std::vector<HudHitBox>& hit_boxes() const { return hits_; }
    // The AlertJump hotkey: the Jump of the strip's alert of the previous build (RadarCameraJump to its position). False
    // when the strip shows no alert with a position (nothing emitted).
    bool jump(std::vector<HudAction>& actions) const;

protected:
    ModernHudView() = default;
    ~ModernHudView() = default;
    ModernHudView(const ModernHudView&) = default;
    ModernHudView& operator=(const ModernHudView&) = default;
    // `table` must outlive the HUD; `text` may be null (port strings in English, retail keys as the keys).
    void init_view(const ComponentTable& table, const TextureAtlas& atlas, FontCache& fonts, const TextDatabase* text);
    // A build's start: no draws, hit boxes, swallowed tiles, badges or Jump yet.
    void begin_build(uint8_t faction_alt);
    int32_t font() const;
    float text_height_units() const;
    Rgba8 accent(uint8_t alpha) const;
    void hit(int32_t id, uint32_t layer, Vec2f center, Vec2f size);
    // #587 a deck button (tile()).
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
    void ability_ring(Vec2f c, float radius, float recharge);
    void key_badge(uint16_t slot, Vec2f top_right, uint32_t layer, bool always = false);  // always: even with key hints off
    // A top-row control ending at canvas x `right` (its back lit while `selected`, its hit box `id`). Returns its width.
    float control_button(int32_t id, std::u32string_view label, bool selected, float right, float y, float h);
    // The HUD style switch "Modern | Classic" ending at `right`, Modern lit (kModernStyleSwitchHit). Returns its width.
    float style_switch(float right, float y, float h);
    // The alert strip (top centre): the newest of `alerts` with a mark by kind, the count of the others and, for an alert
    // with a position, Jump (its live hotkey badge, slot AlertJump). Remembers that position for jump().
    ModernAlertStrip alert_strip(std::span<const HudAlert> alerts);

    const ComponentTable* table_ = nullptr;
    const TextureAtlas* atlas_ = nullptr;
    const HotkeyTable* keys_ = nullptr;
    FontCache* fonts_ = nullptr;
    const TextDatabase* text_ = nullptr;
    HudCanvas canvas_ = HudCanvas::make(1024, 768);
    float text_scale_ = kModernTextScaleDefault;
    ModernPalette colours_;  // set_options: modern_palette
    ModernHudOptions options_;
    uint8_t faction_alt_ = 1;  // the accent's faction (the feed's faction_alt)
    HudDrawList dl_;
    std::vector<HudHitBox> hits_;
    int32_t hover_ = -1;
    std::vector<int32_t> dead_actions_;  // tiles drawn disabled (their clicks are swallowed)
    uint32_t key_badges_ = 0;            // key badges drawn this build
    bool jump_ok_ = false;               // the strip's alert has a position
    Vec2f jump_at_{};
};

}  // namespace alamo::ui
