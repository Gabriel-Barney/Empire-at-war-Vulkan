#include "alamo/ui/modern_tactical_hud.hpp"

#include <algorithm>

#include "alamo/core/ascii.hpp"
#include "alamo/ui/hud_font.hpp"
#include "alamo/ui/orders.hpp"

namespace alamo::ui {

namespace {

// [port: Gabriel] Modern look (#520 design canvas "Modern HUD redesign" v2): dark translucent panels with an edge in the
// local player's faction colour.
constexpr Rgba8 kBack{10, 14, 22, 200};
constexpr Rgba8 kButtonBack{24, 32, 46, 230};
constexpr Rgba8 kText{236, 240, 244, 255};
constexpr Rgba8 kLabel{170, 182, 198, 255};
constexpr Rgba8 kPausedBar{196, 176, 8, 245};  // the Classic pause banner's yellow bar
// By TacticalHudFrame::faction_alt (GameUi::set_faction): 0 the good side (Rebel), 1 any other (Empire, Underworld).
constexpr Rgba8 kAccent[2] = {{226, 120, 48, 255}, {92, 156, 230, 255}};
constexpr uint32_t kPanelLayer = 3, kControlLayer = 4, kBannerLayer = 11;  // the banner over the panels, as Classic's
constexpr float kMargin = 8.0f;        // canvas units from the screen edge
constexpr float kPad = 8.0f;           // inside a panel
constexpr float kDeckHeight = 120.0f;  // canvas units

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
    fast_forward_ = table.find("b_fast_forward_t");
    resume_ = table.find("attack_button");
    if (const int32_t c = table.find("Text_Credits_tactical"); c >= 0 && table[static_cast<size_t>(c)].has_text_color)
        credits_color_ = table[static_cast<size_t>(c)].text_color;
}

void ModernTacticalHud::resize(uint32_t width, uint32_t height) { resize_canvas(canvas_, nullptr, width, height); }

void ModernTacticalHud::set_text_scale(float s) {
    text_scale_ = std::clamp(s > 0.0f ? s : kModernTextScaleDefault, kModernTextScaleMin, kModernTextScaleMax);
}

// The text height in canvas units: kModernTextPx x the text scale pixels at 1080p, following the canvas text height as
// the Classic fonts do (so the UI scale's text growth applies too).
float ModernTacticalHud::text_height_units() const {
    return kModernTextPx * text_scale_ * static_cast<float>(canvas_.font_height()) / 1080.0f * canvas_.units_per_pixel_y;
}

int32_t ModernTacticalHud::font() const {
    if (!fonts_) return -1;
    // font_pixel_height(pt, h) = pt x h / 450: the point size whose pixel height is kModernTextPx x scale at 1080p.
    const float pt = kModernTextPx * text_scale_ * 450.0f / 1080.0f;
    return fonts_->font(fonts_->defaults().text_face, pt, canvas_.font_height());
}

Rgba8 ModernTacticalHud::accent(uint8_t alpha) const {
    Rgba8 c = kAccent[faction_alt_ ? 1 : 0];
    c.a = alpha;
    return c;
}

void ModernTacticalHud::hit(int32_t id, uint32_t layer, Vec2f center, Vec2f size) {
    hits_.push_back(make_hit_box(id, layer, center, size));
}

void ModernTacticalHud::build(const TacticalHudFrame& frame, std::span<const HudUnit>, std::span<const HudTypeInfo>) {
    dl_.clear();
    hits_.clear();
    stats_ = {};
    paused_ = frame.paused;
    faction_alt_ = frame.faction_alt;
    if (!table_ || !fonts_ || frame.world.interface_hidden || font() < 0) return;
    build_deck();
    build_status(frame);
    build_controls(frame);
    build_pause(frame);
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
    if (!stats_.credits_text.empty()) rows.push_back({{}, stats_.credits_text, credits_color_});
    if (!stats_.pop_text.empty()) rows.push_back({text_or_port(text_, nullptr, U"Population"), stats_.pop_text, kText});
    if (!stats_.tech_text.empty()) rows.push_back({{}, stats_.tech_text, kText});
    if (rows.empty()) return;
    HudTextStyle value_style, label_style;
    value_style.font = label_style.font = font();
    label_style.color = kLabel;
    const float line = text_height_units() * 1.5f;
    float width = 0.0f;
    for (const Row& r : rows) {
        float w = measure_text_block(*fonts_, canvas_, r.value, 0.0f, value_style).width;
        if (!r.label.empty()) w += measure_text_block(*fonts_, canvas_, r.label, 0.0f, label_style).width + kPad;
        width = std::max(width, w);
    }
    const Vec2f size{width + 2.0f * kPad, line * static_cast<float>(rows.size()) + kPad};
    const float hw = 0.5f * canvas_.width, hh = 0.5f * canvas_.height;
    const Vec2f center{-hw + kMargin + 0.5f * size.x, hh - kMargin - 0.5f * size.y};
    frame_rect(dl_, kPanelLayer, center, size, kBack, accent(200));
    hit(kModernPanelHit, kPanelLayer, center, size);
    float y = center.y + 0.5f * size.y - 0.5f * kPad - 0.5f * line;
    const float left = center.x - 0.5f * size.x + kPad;
    for (const Row& r : rows) {
        float x = left;
        if (!r.label.empty()) x += add_text(dl_, kPanelLayer, HudList::Text, *fonts_, canvas_, r.label, {x, y}, label_style) + kPad;
        value_style.color = r.color;
        add_text(dl_, kPanelLayer, HudList::Text, *fonts_, canvas_, r.value, {x, y}, value_style);
        y -= line;
    }
    stats_.status_corner = true;
}

void ModernTacticalHud::build_controls(const TacticalHudFrame& f) {
    const float h = text_height_units() * 2.0f;
    const float hw = 0.5f * canvas_.width, hh = 0.5f * canvas_.height;
    const float y = hh - kMargin - 0.5f * h;
    float right = hw - kMargin;  // laid out right to left
    HudTextStyle st;
    st.font = font();
    st.color = kText;
    st.align = TextAlign::Center;
    auto button = [&](int32_t id, std::u32string_view label, bool selected) {
        const float w = std::max(h, measure_text_block(*fonts_, canvas_, label, 0.0f, st).width + 2.0f * kPad);
        const Vec2f c{right - 0.5f * w, y};
        frame_rect(dl_, kControlLayer, c, {w, h}, selected ? accent(220) : kButtonBack, hover_ == id ? kText : accent(200));
        add_text(dl_, kControlLayer, HudList::Text, *fonts_, canvas_, label, c, st);
        hit(id, kControlLayer, c, {w, h});
        right -= w + 0.5f * kPad;
        ++stats_.controls;
    };
    // Menu, pause and fast forward: the Classic components (b_option_t, b_play_pause_t, b_fast_forward_t).
    if (option_ >= 0) button(option_, text_or_port(text_, nullptr, U"Menu"), false);
    if (pause_ >= 0) button(pause_, f.paused ? U">" : U"||", f.paused);
    if (fast_forward_ >= 0) button(fast_forward_, U">>", false);
    // The HUD style switch: "Modern | Classic" with Modern lit; a click switches to Classic.
    right -= kPad;
    const std::u32string modern = text_or_port(text_, nullptr, U"Modern"), classic = text_or_port(text_, nullptr, U"Classic");
    const float modern_w = measure_text_block(*fonts_, canvas_, modern, 0.0f, st).width + 2.0f * kPad;
    const float classic_w = measure_text_block(*fonts_, canvas_, classic, 0.0f, st).width + 2.0f * kPad;
    const Vec2f size{modern_w + classic_w, h};
    const Vec2f c{right - 0.5f * size.x, y};
    frame_rect(dl_, kControlLayer, c, size, kButtonBack, hover_ == kModernStyleSwitchHit ? kText : accent(200));
    const float modern_x = c.x - 0.5f * size.x + 0.5f * modern_w, classic_x = c.x + 0.5f * size.x - 0.5f * classic_w;
    HudQuad lit;
    lit.min = {modern_x - 0.5f * modern_w + 2.0f, y - 0.5f * h + 2.0f};
    lit.max = {modern_x + 0.5f * modern_w - 2.0f, y + 0.5f * h - 2.0f};
    lit.color = accent(220);
    dl_.add(kControlLayer, HudList::Alpha, lit);
    add_text(dl_, kControlLayer, HudList::Text, *fonts_, canvas_, modern, {modern_x, y}, st);
    HudTextStyle dim = st;
    dim.color = kLabel;
    add_text(dl_, kControlLayer, HudList::Text, *fonts_, canvas_, classic, {classic_x, y}, dim);
    hit(kModernStyleSwitchHit, kControlLayer, c, size);
    stats_.style_switch = true;
}

void ModernTacticalHud::build_deck() {
    const Vec2f size{canvas_.width - 2.0f * kMargin, kDeckHeight};
    const Vec2f center{0.0f, -0.5f * canvas_.height + kMargin + 0.5f * size.y};
    frame_rect(dl_, kPanelLayer, center, size, kBack, accent(200));
    hit(kModernPanelHit, kPanelLayer, center, size);
    stats_.deck = true;
}

// The Classic banner's content (TEXT_GAME_PAUSED, Resume Game on attack_button) at the top centre, below the controls row.
void ModernTacticalHud::build_pause(const TacticalHudFrame& f) {
    if (!f.paused || !f.pause_banner) return;
    HudTextStyle st;
    st.font = font();
    st.color = Rgba8{20, 18, 4, 255};
    st.align = TextAlign::Center;
    const std::u32string paused = text_or_port(text_, "TEXT_GAME_PAUSED", U"Game Paused");
    const std::u32string resume = text_or_port(text_, "TEXT_BUTTON_RESUME_GAME", U"Resume Game");
    const float h = text_height_units() * 2.0f;
    const float top = 0.5f * canvas_.height - kMargin - h - kPad;  // under the controls row
    const Vec2f bar_size{measure_text_block(*fonts_, canvas_, paused, 0.0f, st).width + 6.0f * kPad, h};
    const Vec2f bar{0.0f, top - 0.5f * h};
    frame_rect(dl_, kBannerLayer, bar, bar_size, kPausedBar, accent(230));
    add_text(dl_, kBannerLayer, HudList::Text, *fonts_, canvas_, paused, bar, st);
    if (resume_ >= 0) {
        const Vec2f size{measure_text_block(*fonts_, canvas_, resume, 0.0f, st).width + 4.0f * kPad, h};
        const Vec2f c{0.0f, bar.y - h - kPad};
        frame_rect(dl_, kBannerLayer, c, size, hover_ == resume_ ? accent(240) : kButtonBack, accent(230));
        HudTextStyle bs = st;
        bs.color = kText;
        add_text(dl_, kBannerLayer, HudList::Text, *fonts_, canvas_, resume, c, bs);
        hit(resume_, kBannerLayer, c, size);
    }
    stats_.pause_banner = true;
}

int32_t ModernTacticalHud::hit_test(int32_t px, int32_t py) const { return topmost_hit(hits_, canvas_, px, py); }

void ModernTacticalHud::update_input(const RoutedInput& routed, bool shift, bool ctrl, std::vector<commands::EventData>& out,
                                     std::vector<HudAction>* actions) {
    hover_ = routed.hover_component;
    if (!table_) return;
    auto drawn = [this](int32_t id) {
        return std::any_of(hits_.begin(), hits_.end(), [id](const HudHitBox& h) { return h.component == id; });
    };
    auto act = [&](HudAction a) {
        if (actions) actions->push_back(std::move(a));
    };
    for (const HudPointerEvent& e : routed.hud) {
        if (e.kind != HudPointerEvent::Kind::Click || e.component < 0 || !drawn(e.component)) continue;
        if (e.component == kModernStyleSwitchHit) {
            if (e.button == Button::Left) {
                HudAction h;
                h.kind = HudAction::Kind::HudStyleToggle;
                h.component = e.component;
                act(std::move(h));
            }
            continue;
        }
        if (e.component >= kModernHitBase) continue;  // a panel's back
        const std::string& name = (*table_)[static_cast<size_t>(e.component)].name;
        // The Classic clicks of the shared controls (TacticalHud::update_input): Resume Game while paused, pause through
        // the command stream, the other buttons as a Button action by component name.
        if (e.component == resume_ && e.button == Button::Left) {
            if (paused_) {
                HudAction h;
                h.kind = HudAction::Kind::ResumeGame;
                h.component = e.component;
                h.name = name;
                act(std::move(h));
            }
            continue;
        }
        if (e.component == pause_ && e.button == Button::Left) {
            out.push_back(pause_event(!paused_));
            continue;
        }
        HudAction h;
        h.kind = HudAction::Kind::Button;
        h.component = e.component;
        h.name = name;
        h.button = e.button;
        h.shift = shift;
        h.ctrl = ctrl;
        act(std::move(h));
    }
}

}  // namespace alamo::ui
