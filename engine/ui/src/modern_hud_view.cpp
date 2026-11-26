#include "alamo/ui/modern_hud_view.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "alamo/core/ascii.hpp"
#include "alamo/ui/gui_menus.hpp"
#include "alamo/ui/hud_font.hpp"
#include "modern_hud_draw.hpp"
#include "ui_strings.hpp"

namespace alamo::ui {

namespace modern_draw {

void add_rect(HudDrawList& dl, uint32_t layer, float x0, float y0, float x1, float y1, Rgba8 color) {
    HudQuad q;
    q.min = {x0, y0};
    q.max = {x1, y1};
    q.color = color;
    dl.add(layer, HudList::Alpha, q);
}

// A rectangle outline of dashes (a disabled control, readable without colour).
void dashed_rect(HudDrawList& dl, uint32_t layer, Vec2f c, Vec2f size, Rgba8 color) {
    constexpr float kDash = 6.0f, kGap = 4.0f, kT = 2.0f;
    const float x0 = c.x - 0.5f * size.x, x1 = c.x + 0.5f * size.x, y0 = c.y - 0.5f * size.y, y1 = c.y + 0.5f * size.y;
    for (float x = x0; x < x1; x += kDash + kGap) {
        const float e = std::min(x + kDash, x1);
        add_rect(dl, layer, x, y1 - kT, e, y1, color);
        add_rect(dl, layer, x, y0, e, y0 + kT, color);
    }
    for (float y = y0; y < y1; y += kDash + kGap) {
        const float e = std::min(y + kDash, y1);
        add_rect(dl, layer, x0, y, x0 + kT, e, color);
        add_rect(dl, layer, x1 - kT, y, x1, e, color);
    }
}

std::u32string count_text(uint32_t n) {
    char b[16];
    std::snprintf(b, sizeof b, "%u", n);
    return ascii32(b);
}

}  // namespace modern_draw

using namespace modern_draw;

void ModernHudView::init_view(const ComponentTable& table, const TextureAtlas& atlas, FontCache& fonts, const TextDatabase* text) {
    table_ = &table;
    atlas_ = &atlas;
    fonts_ = &fonts;
    text_ = text;
    set_options(options_);  // the palette from the options' opacity
}

bool ModernHudView::drawn(int32_t id) const {
    return std::any_of(hits_.begin(), hits_.end(), [id](const HudHitBox& h) { return h.component == id; });
}

bool ModernHudView::view_click(const HudPointerEvent& e, std::vector<HudAction>* actions) const {
    if (e.component == kModernStyleSwitchHit) {
        if (e.button == Button::Left && actions) {
            HudAction h;
            h.kind = HudAction::Kind::HudStyleToggle;
            h.component = e.component;
            actions->push_back(std::move(h));
        }
        return true;
    }
    if (e.component == kModernJumpHit) {
        if (e.button == Button::Left && actions) jump(*actions);
        return true;
    }
    return false;
}

HudAction ModernHudView::button_action(const HudPointerEvent& e, bool shift, bool ctrl) const {
    HudAction h;
    h.kind = HudAction::Kind::Button;
    h.component = e.component;
    h.name = (*table_)[static_cast<size_t>(e.component)].name;
    h.button = e.button;
    h.shift = shift;
    h.ctrl = ctrl;
    return h;
}

void ModernHudView::begin_build(uint8_t faction_alt) {
    dl_.clear();
    hits_.clear();
    dead_actions_.clear();
    key_badges_ = 0;
    jump_ok_ = false;  // a hidden HUD has no Jump
    faction_alt_ = faction_alt;
}

void HudAlertLog::push(HudAlert::Kind kind, std::u32string text, double now) {
    if (text.empty()) return;
    HudAlert a;
    a.kind = kind;
    a.text = std::move(text);
    a.time = now;
    items_.push_back(std::move(a));
    if (items_.size() > kMax) items_.erase(items_.begin(), items_.end() - static_cast<std::ptrdiff_t>(kMax));
}

void HudAlertLog::push_at(HudAlert::Kind kind, std::u32string text, float x, float y, double now) {
    if (text.empty()) return;
    push(kind, std::move(text), now);
    items_.back().has_position = true;
    items_.back().x = x;
    items_.back().y = y;
}

void HudAlertLog::update(double now) {
    for (HudAlert& a : items_) a.age = static_cast<float>(std::max(0.0, now - a.time));
    std::erase_if(items_, [](const HudAlert& a) { return a.age > static_cast<float>(kSeconds); });
}

void ModernHudView::resize(uint32_t width, uint32_t height) { resize_canvas(canvas_, nullptr, width, height); }

void ModernHudView::set_text_scale(float s) {
    text_scale_ = std::clamp(s > 0.0f ? s : kModernTextScaleDefault, kModernTextScaleMin, kModernTextScaleMax);
    options_.text_scale = static_cast<int32_t>(std::lround(text_scale_ * 100.0f));
}

// The text height in canvas units: kModernTextPx x the text scale pixels at 1080p, following the canvas text height as
// the Classic fonts do (so the UI scale's text growth applies too).
float ModernHudView::text_height_units() const {
    return kModernTextPx * text_scale_ * static_cast<float>(canvas_.font_height()) / 1080.0f * canvas_.units_per_pixel_y;
}

float ModernHudView::px_units(float px) const {
    return px * static_cast<float>(canvas_.font_height()) / 1080.0f * canvas_.units_per_pixel_y;
}

int32_t ModernHudView::font() const {
    if (!fonts_) return -1;
    // font_pixel_height(pt, h) = pt x h / 450: the point size whose pixel height is kModernTextPx x scale at 1080p.
    const float pt = kModernTextPx * text_scale_ * 450.0f / 1080.0f;
    return fonts_->font(fonts_->defaults().text_face, pt, canvas_.font_height());
}

// [port: Gabriel] The Modern look (#520 design canvas "Modern HUD redesign" v2; #590 colour choices): dark translucent
// panels (the panel opacity) with an edge in the local faction's colour; colour-blind safe = the Okabe-Ito palette (blue
// accent, bluish green / orange / vermillion hull); high contrast = near-opaque black panels, white text, yellow accent.
ModernPalette modern_palette(const ModernHudOptions& options) {
    const ModernHudOptions o = clamp_options(options);
    const auto alpha = static_cast<uint8_t>(std::lround(2.55f * static_cast<float>(o.panel_opacity)));
    ModernPalette c;
    c.back = {10, 14, 22, alpha};
    c.button = {24, 32, 46, static_cast<uint8_t>(std::min(255, alpha + 30))};
    switch (o.colours) {
        case HudColours::ColourBlind:
            c.faction_accent = false;
            c.accent = {0, 114, 178, 255};
            c.shield = {86, 180, 233, 255};
            c.hull_ok = {0, 158, 115, 255};
            c.hull_warn = {230, 159, 0, 255};
            c.hull_bad = {213, 94, 0, 255};
            break;
        case HudColours::HighContrast:
            c.faction_accent = false;
            c.back = {0, 0, 0, std::max<uint8_t>(alpha, 240)};
            c.button = {0, 0, 0, 255};
            c.text = {255, 255, 255, 255};
            c.label = {230, 230, 230, 255};
            c.disabled = {150, 150, 150, 255};
            c.accent = {255, 230, 0, 255};
            break;
        case HudColours::Faction: break;
    }
    return c;
}

Rgba8 ModernPalette::edge(uint8_t faction_alt, uint8_t alpha) const {
    Rgba8 c = faction_accent ? kModernAccent[faction_alt ? 1 : 0] : accent;
    c.a = alpha;
    return c;
}

void ModernHudView::set_options(const ModernHudOptions& o) {
    options_ = clamp_options(o);
    text_scale_ = static_cast<float>(options_.text_scale) / 100.0f;
    colours_ = modern_palette(options_);
}

Rgba8 ModernHudView::accent(uint8_t alpha) const {
    return colours_.edge(faction_alt_, alpha);  // by the feed's faction_alt (GameUi::set_faction)
}

void ModernHudView::hit(int32_t id, uint32_t layer, Vec2f center, Vec2f size) {
    hits_.push_back(make_hit_box(id, layer, center, size));
}

// #587 a deck button: its back (the selected look: filled, with a corner notch; disabled: a dashed outline), the icon or
// text, a countdown ring, a count badge and the key badge.
void ModernHudView::tile(const Tile& t) {
    const Vec2f c = t.c, s = t.size;
    if (!t.enabled) {
        add_rect(dl_, kControlLayer, c.x - 0.5f * s.x, c.y - 0.5f * s.y, c.x + 0.5f * s.x, c.y + 0.5f * s.y, Rgba8{16, 20, 28, 160});
        dashed_rect(dl_, kControlLayer, c, s, colours_.disabled);
    } else {
        frame_rect(dl_, kControlLayer, c, s, t.selected ? accent(150) : colours_.button,
                   hover_ == t.id || t.selected ? colours_.text : accent(200));
    }
    if (t.selected) {
        HudQuad notch;
        notch.has_corners = true;
        const float n = 0.3f * std::min(s.x, s.y), x0 = c.x - 0.5f * s.x, y1 = c.y + 0.5f * s.y;
        notch.corners = {Vec2f{x0, y1}, Vec2f{x0 + n, y1}, Vec2f{x0 + n, y1}, Vec2f{x0, y1 - n}};
        notch.color = colours_.text;
        dl_.add(kControlLayer, HudList::Alpha, notch);
    }
    const float th = text_height_units();
    HudTextStyle st;
    st.font = font();
    st.color = t.enabled ? colours_.text : colours_.disabled;
    st.align = TextAlign::Center;
    float text_y = c.y;
    if (!t.icon.empty() && atlas_) {
        const float side = std::min(s.x, s.y - (t.text.empty() ? 0.0f : th)) * 0.75f;
        add_atlas_quad(dl_, kControlLayer, HudList::Alpha, *atlas_, t.icon, {c.x, c.y + (t.text.empty() ? 0.0f : 0.5f * th)},
                       {side, side}, 1.0f, t.enabled ? Rgba8{255, 255, 255, 255} : Rgba8{128, 128, 128, 255});
        text_y = c.y - 0.5f * s.y + 0.6f * th;
    }
    if (t.ring >= 0.0f) ability_ring(c, 0.5f * std::min(s.x, s.y) - 2.0f, std::clamp(t.ring, 0.0f, 1.0f));
    if (!t.text.empty()) {
        st.width_scale = fit_width_scale(measure_text_block(*fonts_, canvas_, t.text, 0.0f, st).width, s.x - kPad);
        add_text(dl_, kControlLayer, HudList::Text, *fonts_, canvas_, t.text, {c.x, text_y}, st);
    }
    if (!t.badge.empty()) {
        HudTextStyle bs = st;
        bs.width_scale = 1.0f;
        const float bw = measure_text_block(*fonts_, canvas_, t.badge, 0.0f, bs).width + kPad, bh = th * 1.1f;
        const Vec2f bc{c.x - 0.5f * s.x + 0.5f * bw + 2.0f, c.y + 0.5f * s.y - 0.5f * bh - 2.0f};
        frame_rect(dl_, kControlLayer, bc, {bw, bh}, Rgba8{0, 0, 0, 200}, accent(220));
        bs.color = colours_.text;
        add_text(dl_, kControlLayer, HudList::Text, *fonts_, canvas_, t.badge, bc, bs);
    }
    if (t.hotkey >= 0) key_badge(static_cast<uint16_t>(t.hotkey), {c.x + 0.5f * s.x - 2.0f, c.y + 0.5f * s.y - 2.0f}, kControlLayer);
    if (!t.enabled) dead_actions_.push_back(t.id);
    hit(t.id, kControlLayer, c, s);
}

// #586 a recharge ring round an ability button: the recharged part (from 12 o'clock, clockwise) in the accent, the rest dim.
void ModernHudView::ability_ring(Vec2f c, float radius, float recharge) {
    constexpr int kSegments = 32;
    const float inner = radius - 3.0f, outer = radius;
    for (int k = 0; k < kSegments; ++k) {
        const float a0 = 6.2831853f * static_cast<float>(k) / kSegments, a1 = 6.2831853f * static_cast<float>(k + 1) / kSegments;
        auto at = [&](float a, float r) { return Vec2f{c.x + r * std::sin(a), c.y + r * std::cos(a)}; };
        HudQuad q;
        q.has_corners = true;
        q.corners = {at(a0, outer), at(a1, outer), at(a1, inner), at(a0, inner)};
        q.color = static_cast<float>(k) + 0.5f < recharge * kSegments ? accent(255) : Rgba8{0, 0, 0, 170};
        dl_.add(kControlLayer, HudList::Alpha, q);
    }
}

// A key badge (the slot's live binding) with its top-right corner at `top_right`.
void ModernHudView::key_badge(uint16_t slot, Vec2f top_right, uint32_t layer, bool always) {
    if (!keys_ || (!options_.key_hints && !always)) return;
    const HotkeyBinding& b = keys_->binding(slot);
    if (b.key < 0) return;
    const std::u32string label = hotkey_label(b, text_);
    if (label.empty()) return;
    HudTextStyle st;
    st.font = font();
    st.color = colours_.text;
    st.align = TextAlign::Center;
    const float h = text_height_units() * 1.2f;
    const float w = std::max(h, measure_text_block(*fonts_, canvas_, label, 0.0f, st).width + kPad);
    const Vec2f c{top_right.x - 0.5f * w, top_right.y - 0.5f * h};
    frame_rect(dl_, layer, c, {w, h}, Rgba8{0, 0, 0, 200}, colours_.label);
    add_text(dl_, layer, HudList::Text, *fonts_, canvas_, label, c, st);
    ++key_badges_;
}

ModernAlertStrip ModernHudView::alert_strip(std::span<const HudAlert> alerts) {
    ModernAlertStrip strip;
    if (alerts.empty()) return strip;
    const HudAlert& a = alerts.back();
    const float h = text_height_units() * 2.0f;
    const float y = 0.5f * canvas_.height - kMargin - 0.5f * h;
    HudTextStyle st;
    st.font = font();
    st.color = colours_.text;
    std::u32string more;
    if (alerts.size() > 1) {
        char n[16];
        std::snprintf(n, sizeof n, "  +%zu", alerts.size() - 1);
        more = ascii32(n);
    }
    const std::u32string jump = text_or_port(text_, nullptr, U"Jump");
    HudTextStyle cs = st;
    cs.align = TextAlign::Center;
    const float max_text = 0.4f * canvas_.width;
    const float text_w = std::min(max_text, measure_text_block(*fonts_, canvas_, a.text, 0.0f, st).width);
    const float more_w = more.empty() ? 0.0f : measure_text_block(*fonts_, canvas_, more, 0.0f, st).width;
    const float jump_w = a.has_position ? measure_text_block(*fonts_, canvas_, jump, 0.0f, cs).width + 6.0f * kPad : 0.0f;
    const float mark = 0.6f * h;  // the kind's mark: a square (advisor), a diamond (radar), a triangle (objective), a bar (production)
    const Vec2f size{mark + text_w + more_w + jump_w + 4.0f * kPad, h};
    const Vec2f c{0.0f, y};
    frame_rect(dl_, kPanelLayer, c, size, colours_.back, accent(220));
    hit(kModernPanelHit, kPanelLayer, c, size);
    float x = c.x - 0.5f * size.x + kPad;
    const Vec2f mc{x + 0.5f * mark, y};
    switch (a.kind) {
        case HudAlert::Kind::Radar: {
            HudQuad q;
            q.has_corners = true;
            q.corners = {Vec2f{mc.x, mc.y + 0.5f * mark}, Vec2f{mc.x + 0.5f * mark, mc.y}, Vec2f{mc.x, mc.y - 0.5f * mark},
                         Vec2f{mc.x - 0.5f * mark, mc.y}};
            q.color = accent(255);
            dl_.add(kPanelLayer, HudList::Alpha, q);
            break;
        }
        case HudAlert::Kind::Advisor:
            add_rect(dl_, kPanelLayer, mc.x - 0.4f * mark, mc.y - 0.4f * mark, mc.x + 0.4f * mark, mc.y + 0.4f * mark, accent(255));
            break;
        case HudAlert::Kind::Objective: {  // a triangle, point up (the fourth corner repeats the apex)
            HudQuad q;
            q.has_corners = true;
            const float d = 0.5f * mark;
            q.corners = {Vec2f{mc.x, mc.y + d}, Vec2f{mc.x, mc.y + d}, Vec2f{mc.x + d, mc.y - d}, Vec2f{mc.x - d, mc.y - d}};
            q.color = accent(255);
            dl_.add(kPanelLayer, HudList::Alpha, q);
            break;
        }
        default:
            add_rect(dl_, kPanelLayer, mc.x - 0.5f * mark, mc.y - 0.15f * mark, mc.x + 0.5f * mark, mc.y + 0.15f * mark, accent(255));
            break;
    }
    x += mark + kPad;
    HudTextStyle ts = st;
    ts.width_scale = fit_width_scale(measure_text_block(*fonts_, canvas_, a.text, 0.0f, st).width, max_text);
    add_text(dl_, kPanelLayer, HudList::Text, *fonts_, canvas_, a.text, {x, y}, ts);
    x += text_w;
    if (!more.empty()) {
        HudTextStyle ms = st;
        ms.color = colours_.label;
        add_text(dl_, kPanelLayer, HudList::Text, *fonts_, canvas_, more, {x, y}, ms);
        x += more_w;
    }
    if (a.has_position) {
        const Vec2f jc{x + kPad + 0.5f * jump_w, y};
        const Vec2f js{jump_w, h - 4.0f};
        frame_rect(dl_, kControlLayer, jc, js, colours_.button, hover_ == kModernJumpHit ? colours_.text : accent(220));
        add_text(dl_, kControlLayer, HudList::Text, *fonts_, canvas_, jump, {jc.x - 1.5f * kPad, y}, cs);
        key_badge(hk::kAlertJump, {jc.x + 0.5f * js.x - 2.0f, jc.y + 0.5f * js.y - 2.0f}, kControlLayer);
        hit(kModernJumpHit, kControlLayer, jc, js);
        jump_ok_ = true;
        jump_at_ = {a.x, a.y};
        strip.jump = true;
    }
    strip.text = a.text;
    strip.more = static_cast<uint32_t>(alerts.size() - 1);
    return strip;
}

bool ModernHudView::jump(std::vector<HudAction>& actions) const {
    if (!jump_ok_) return false;
    HudAction h;
    h.kind = HudAction::Kind::RadarCameraJump;
    h.component = kModernJumpHit;
    h.x = jump_at_.x;
    h.y = jump_at_.y;
    actions.push_back(std::move(h));
    return true;
}

int32_t ModernHudView::hit_test(int32_t px, int32_t py) const { return topmost_hit(hits_, canvas_, px, py); }

float ModernHudView::control_button(int32_t id, std::u32string_view label, bool selected, float right, float y, float h) {
    HudTextStyle st;
    st.font = font();
    st.color = colours_.text;
    st.align = TextAlign::Center;
    const float w = std::max(h, measure_text_block(*fonts_, canvas_, label, 0.0f, st).width + 2.0f * kPad);
    const Vec2f c{right - 0.5f * w, y};
    frame_rect(dl_, kControlLayer, c, {w, h}, selected ? accent(220) : colours_.button, hover_ == id ? colours_.text : accent(200));
    add_text(dl_, kControlLayer, HudList::Text, *fonts_, canvas_, label, c, st);
    hit(id, kControlLayer, c, {w, h});
    return w;
}

// A click on the switch switches to Classic (HudAction::HudStyleToggle).
float ModernHudView::style_switch(float right, float y, float h) {
    HudTextStyle st;
    st.font = font();
    st.color = colours_.text;
    st.align = TextAlign::Center;
    const std::u32string modern = text_or_port(text_, nullptr, U"Modern"), classic = text_or_port(text_, nullptr, U"Classic");
    const float modern_w = measure_text_block(*fonts_, canvas_, modern, 0.0f, st).width + 2.0f * kPad;
    const float classic_w = measure_text_block(*fonts_, canvas_, classic, 0.0f, st).width + 2.0f * kPad;
    const Vec2f size{modern_w + classic_w, h};
    const Vec2f c{right - 0.5f * size.x, y};
    frame_rect(dl_, kControlLayer, c, size, colours_.button, hover_ == kModernStyleSwitchHit ? colours_.text : accent(200));
    const float modern_x = c.x - 0.5f * size.x + 0.5f * modern_w, classic_x = c.x + 0.5f * size.x - 0.5f * classic_w;
    add_rect(dl_, kControlLayer, modern_x - 0.5f * modern_w + 2.0f, y - 0.5f * h + 2.0f, modern_x + 0.5f * modern_w - 2.0f,
             y + 0.5f * h - 2.0f, accent(220));
    add_text(dl_, kControlLayer, HudList::Text, *fonts_, canvas_, modern, {modern_x, y}, st);
    HudTextStyle dim = st;
    dim.color = colours_.label;
    add_text(dl_, kControlLayer, HudList::Text, *fonts_, canvas_, classic, {classic_x, y}, dim);
    hit(kModernStyleSwitchHit, kControlLayer, c, size);
    return size.x;
}

}  // namespace alamo::ui
