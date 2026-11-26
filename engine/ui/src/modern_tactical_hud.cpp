#include "alamo/ui/modern_tactical_hud.hpp"

#include <algorithm>

#include "alamo/core/ascii.hpp"
#include "alamo/ui/hud_font.hpp"
#include "alamo/ui/orders.hpp"

namespace alamo::ui {

namespace {

// [port: Gabriel] Modern look (#520 design canvas "Modern HUD redesign" v2): dark translucent panels with an accent edge
// in the local player's faction colour.
constexpr Rgba8 kBack{10, 14, 22, 200};
constexpr Rgba8 kButtonBack{24, 32, 46, 230};
constexpr Rgba8 kText{236, 240, 244, 255};
constexpr Rgba8 kLabel{170, 182, 198, 255};
constexpr Rgba8 kCredits{255, 212, 33, 255};  // Text_Credits_tactical's Text_Color
// By TacticalHudFrame::faction_alt (GameUi::set_faction): 0 the good side (Rebel), 1 any other (Empire, Underworld).
constexpr Rgba8 kAccent[2] = {{226, 120, 48, 255}, {92, 156, 230, 255}};
constexpr uint32_t kPanelLayer = 3, kControlLayer = 4;
constexpr float kMargin = 8.0f;        // canvas units from the screen edge
constexpr float kPad = 8.0f;           // inside a panel
constexpr float kDeckHeight = 120.0f;  // canvas units

Rgba8 with_alpha(Rgba8 c, uint8_t a) {
    c.a = a;
    return c;
}

}  // namespace

std::string_view hud_style_name(HudStyle s) { return s == HudStyle::Modern ? "modern" : "classic"; }

std::optional<HudStyle> parse_hud_style(std::string_view s) {
    if (ascii_iequals(s, "classic")) return HudStyle::Classic;
    if (ascii_iequals(s, "modern")) return HudStyle::Modern;
    return std::nullopt;
}

void ModernTacticalHud::init(const ComponentTable& table, FontCache& fonts, const TextDatabase* text) {
    table_ = &table;
    fonts_ = &fonts;
    text_ = text;
    option_ = table.find("b_option_t");
    pause_ = table.find("b_play_pause_t");
    fast_ = table.find("b_fast_forward_t");
}

void ModernTacticalHud::resize(uint32_t width, uint32_t height) { resize_canvas(canvas_, nullptr, width, height); }

void ModernTacticalHud::set_text_scale(float s) {
    text_scale_ = std::clamp(s > 0.0f ? s : kModernTextScaleDefault, kModernTextScaleMin, kModernTextScaleMax);
}

// Pixels on this screen for a height of `px_1080` pixels at 1080p and 100 % text scale (the font size follows the
// canvas text height, as the Classic fonts do, so the UI scale's text growth applies too).
float ModernTacticalHud::text_px(float px_1080) const {
    return px_1080 * text_scale_ * static_cast<float>(canvas_.font_height()) / 1080.0f;
}

int32_t ModernTacticalHud::font(float px_1080) const {
    if (!fonts_) return -1;
    // font_pixel_height(pt, h) = pt x h / 450 -> pt for text_px at the canvas text height.
    const float pt = px_1080 * text_scale_ * 450.0f / 1080.0f;
    return fonts_->font(fonts_->defaults().text_face, pt, canvas_.font_height());
}

void ModernTacticalHud::hit(int32_t id, uint32_t layer, Vec2f center, Vec2f size) {
    hits_.push_back(make_hit_box(id, layer, center, size));
}

void ModernTacticalHud::build(const TacticalHudFrame& frame, std::span<const HudUnit>, std::span<const HudTypeInfo>) {
    dl_.clear();
    hits_.clear();
    stats_ = {};
    paused_ = frame.paused;
    if (!table_ || !fonts_ || frame.world.interface_hidden) return;
    build_deck(frame);
    build_status(frame);
    build_controls(frame);
    stats_.hit_boxes = static_cast<uint32_t>(hits_.size());
    stats_.quads = static_cast<uint32_t>(dl_.total());
}

void ModernTacticalHud::build_status(const TacticalHudFrame& f) {
    struct Row {
        std::u32string label, value;
        Rgba8 color;
    };
    stats_.credits_text = f.credits >= 0 ? hud_credits_text(f.credits) : std::u32string();
    stats_.pop_text = hud_pop_text(f.pop, f.pop_cap);
    stats_.tech_text = hud_tech_text(text_, f.tech);
    std::vector<Row> rows;
    if (!stats_.credits_text.empty()) rows.push_back({{}, stats_.credits_text, kCredits});
    if (!stats_.pop_text.empty()) rows.push_back({text_or_port(text_, nullptr, U"Population"), stats_.pop_text, kText});
    if (!stats_.tech_text.empty()) rows.push_back({{}, stats_.tech_text, kText});
    if (rows.empty()) return;
    const int32_t body = font(kModernBodyPx), label = font(kModernLabelPx);
    if (body < 0 || label < 0) return;
    HudTextStyle bs, ls;
    bs.font = body;
    bs.color = kText;
    ls.font = label;
    ls.color = kLabel;
    const float upp = canvas_.units_per_pixel_y;
    const float line = text_px(kModernBodyPx) * 1.5f * upp;
    float width = 0.0f;
    for (const Row& r : rows) {
        float w = measure_text_block(*fonts_, canvas_, r.value, 0.0f, bs).width;
        if (!r.label.empty()) w += measure_text_block(*fonts_, canvas_, r.label, 0.0f, ls).width + kPad;
        width = std::max(width, w);
    }
    const Vec2f size{width + 2.0f * kPad, line * static_cast<float>(rows.size()) + kPad};
    const float hw = 0.5f * canvas_.width, hh = 0.5f * canvas_.height;
    const Vec2f center{-hw + kMargin + 0.5f * size.x, hh - kMargin - 0.5f * size.y};
    frame_rect(dl_, kPanelLayer, center, size, kBack, with_alpha(kAccent[f.faction_alt ? 1 : 0], 200));
    hit(kModernPanelHit, kPanelLayer, center, size);
    float y = center.y + 0.5f * size.y - 0.5f * kPad - 0.5f * line;
    const float left = center.x - 0.5f * size.x + kPad;
    for (const Row& r : rows) {
        float x = left;
        if (!r.label.empty()) x += add_text(dl_, kPanelLayer, HudList::Text, *fonts_, canvas_, r.label, {x, y}, ls) + kPad;
        HudTextStyle vs = bs;
        vs.color = r.color;
        add_text(dl_, kPanelLayer, HudList::Text, *fonts_, canvas_, r.value, {x, y}, vs);
        y -= line;
    }
    stats_.status_corner = true;
}

void ModernTacticalHud::build_controls(const TacticalHudFrame& f) {
    const int32_t body = font(kModernBodyPx);
    if (body < 0) return;
    const Rgba8 accent = kAccent[f.faction_alt ? 1 : 0];
    const float upp = canvas_.units_per_pixel_y;
    const float h = text_px(kModernBodyPx) * 2.0f * upp;
    const float hw = 0.5f * canvas_.width, hh = 0.5f * canvas_.height;
    const float y = hh - kMargin - 0.5f * h;
    float right = hw - kMargin;  // laid out right to left
    HudTextStyle st;
    st.font = body;
    st.color = kText;
    st.align = TextAlign::Center;
    auto button = [&](int32_t id, std::u32string_view label, bool selected) {
        const float w = std::max(h, measure_text_block(*fonts_, canvas_, label, 0.0f, st).width + 2.0f * kPad);
        const Vec2f c{right - 0.5f * w, y};
        const bool hot = hover_ == id;
        frame_rect(dl_, kControlLayer, c, {w, h}, selected ? with_alpha(accent, 220) : kButtonBack,
                   hot ? kText : with_alpha(accent, 200));
        add_text(dl_, kControlLayer, HudList::Text, *fonts_, canvas_, label, c, st);
        hit(id, kControlLayer, c, {w, h});
        right -= w + 0.5f * kPad;
    };
    // Menu, pause and fast forward: the Classic components (b_option_t, b_play_pause_t, b_fast_forward_t).
    if (option_ >= 0) {
        button(option_, text_or_port(text_, nullptr, U"Menu"), false);
        ++stats_.controls;
    }
    if (pause_ >= 0) {
        button(pause_, f.paused ? U">" : U"||", f.paused);
        ++stats_.controls;
    }
    if (fast_ >= 0) {
        button(fast_, U">>", false);
        ++stats_.controls;
    }
    // The HUD style switch: "Modern | Classic" with Modern lit; a click switches to Classic.
    right -= kPad;
    const std::u32string modern = text_or_port(text_, nullptr, U"Modern"), classic = text_or_port(text_, nullptr, U"Classic");
    const float mw = measure_text_block(*fonts_, canvas_, modern, 0.0f, st).width + 2.0f * kPad;
    const float cw = measure_text_block(*fonts_, canvas_, classic, 0.0f, st).width + 2.0f * kPad;
    const Vec2f sc{right - 0.5f * (mw + cw), y};
    const bool hot = hover_ == kModernStyleSwitchHit;
    frame_rect(dl_, kControlLayer, sc, {mw + cw, h}, kButtonBack, hot ? kText : with_alpha(accent, 200));
    const Vec2f mc{sc.x - 0.5f * (mw + cw) + 0.5f * mw, y}, cc{sc.x + 0.5f * (mw + cw) - 0.5f * cw, y};
    HudQuad lit;
    lit.min = {mc.x - 0.5f * mw + 2.0f, y - 0.5f * h + 2.0f};
    lit.max = {mc.x + 0.5f * mw - 2.0f, y + 0.5f * h - 2.0f};
    lit.color = with_alpha(accent, 220);
    dl_.add(kControlLayer, HudList::Alpha, lit);
    add_text(dl_, kControlLayer, HudList::Text, *fonts_, canvas_, modern, mc, st);
    HudTextStyle cs = st;
    cs.color = kLabel;
    add_text(dl_, kControlLayer, HudList::Text, *fonts_, canvas_, classic, cc, cs);
    hit(kModernStyleSwitchHit, kControlLayer, sc, {mw + cw, h});
    stats_.style_switch = true;
}

void ModernTacticalHud::build_deck(const TacticalHudFrame& f) {
    const float hh = 0.5f * canvas_.height;
    const Vec2f size{canvas_.width - 2.0f * kMargin, kDeckHeight};
    const Vec2f center{0.0f, -hh + kMargin + 0.5f * size.y};
    frame_rect(dl_, kPanelLayer, center, size, kBack, with_alpha(kAccent[f.faction_alt ? 1 : 0], 200));
    hit(kModernPanelHit, kPanelLayer, center, size);
    stats_.deck = true;
}

int32_t ModernTacticalHud::hit_test(int32_t px, int32_t py) const { return topmost_hit(hits_, canvas_, px, py); }

void ModernTacticalHud::update_input(const RoutedInput& routed, bool shift, bool ctrl, double,
                                     std::vector<commands::EventData>& out, std::vector<HudAction>* actions) {
    hover_ = routed.hover_component;
    if (!table_) return;
    auto drawn = [this](int32_t id) {
        return std::any_of(hits_.begin(), hits_.end(), [id](const HudHitBox& h) { return h.component == id; });
    };
    for (const HudPointerEvent& e : routed.hud) {
        if (e.kind != HudPointerEvent::Kind::Click || e.component < 0 || !drawn(e.component)) continue;
        if (e.component == kModernStyleSwitchHit) {
            if (e.button == Button::Left && actions) {
                HudAction h;
                h.kind = HudAction::Kind::HudStyleToggle;
                h.component = e.component;
                actions->push_back(std::move(h));
            }
            continue;
        }
        if (e.component >= kModernHitBase) continue;  // a panel's back
        // The Classic clicks of the shared controls (TacticalHud::update_input): pause through the command stream, the
        // other buttons as a Button action by component name.
        if (e.component == pause_ && e.button == Button::Left) {
            out.push_back(pause_event(!paused_));
            continue;
        }
        if (!actions) continue;
        HudAction h;
        h.kind = HudAction::Kind::Button;
        h.component = e.component;
        h.name = (*table_)[static_cast<size_t>(e.component)].name;
        h.button = e.button;
        h.shift = shift;
        h.ctrl = ctrl;
        actions->push_back(std::move(h));
    }
}

}  // namespace alamo::ui
