#include "alamo/ui/credits_roll.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <string>

#include "alamo/data/game_database.hpp"
#include "alamo/ui/hud_font.hpp"

namespace alamo::ui {

namespace {

uint32_t rd32(std::span<const uint8_t> b, size_t at) {
    uint32_t v = 0;
    std::memcpy(&v, b.data() + at, 4);
    return v;
}

// "r, g, b" in 0..1 -> RGBA8 (alpha 255); a missing / malformed value keeps `fallback`.
Rgba8 unit_color(const data::GameConstants& gc, std::string_view tag, Rgba8 fallback) {
    const auto v = gc.get(tag);
    if (!v) return fallback;
    float c[3] = {0, 0, 0};
    size_t n = 0;
    std::string s(*v);
    for (char& ch : s)
        if (ch == ',') ch = ' ';
    const char* p = s.c_str();
    char* end = nullptr;
    for (; n < 3; ++n) {
        const float f = std::strtof(p, &end);
        if (end == p) break;
        c[n] = f;
        p = end;
    }
    if (n < 3) return fallback;
    auto u8 = [](float f) { return static_cast<uint8_t>(std::lround(std::clamp(f, 0.0f, 1.0f) * 255.0f)); };
    return {u8(c[0]), u8(c[1]), u8(c[2]), 255};
}

}  // namespace

CreditsStyle credits_style(const data::GameConstants& gc) {
    CreditsStyle s;
    if (const auto f = gc.get("Credits_Font"); f && !f->empty()) s.font = std::string(*f);
    s.font_size = std::trunc(gc.get_float("Credits_Font_Size", s.font_size));
    s.header_top = unit_color(gc, "Credits_Header_Top_Color", s.header_top);
    s.header_bottom = unit_color(gc, "Credits_Header_Bottom_Color", s.header_bottom);
    s.top = unit_color(gc, "Credits_Top_Color", s.top);
    s.bottom = unit_color(gc, "Credits_Bottom_Color", s.bottom);
    s.spacing = gc.get_float("Credits_Spacing", s.spacing);
    s.margin = gc.get_float("Credits_Margin", s.margin);
    s.scroll_rate = gc.get_float("Credits_Scroll_Rate", s.scroll_rate);
    for (size_t i = 0; i < s.logos.size(); ++i) {  // #458
        const std::string k = "Credits_Logo_" + std::to_string(i + 1) + "_";
        CreditsLogo& l = s.logos[i];
        if (const auto n = gc.get(k + "Name")) l.name = std::string(*n);
        l.width = gc.get_float(k + "Width", l.width);
        l.height = gc.get_float(k + "Height", l.height);
        l.y_offset = gc.get_float(k + "Y_Offset", l.y_offset);
    }
    return s;
}

bool parse_credits(std::span<const uint8_t> b, std::vector<CreditsLine>& out) {
    out.clear();
    if (b.size() < 4) return false;
    const uint32_t n = rd32(b, 0);
    if (n > (b.size() - 4) / 12) return false;
    size_t values = 4 + static_cast<size_t>(n) * 12, total_chars = 0, total_keys = 0;
    for (uint32_t i = 0; i < n; ++i) {
        total_chars += rd32(b, 4 + 12 * i + 4);
        total_keys += rd32(b, 4 + 12 * i + 8);
    }
    if (values + total_chars * 2 + total_keys > b.size()) return false;
    size_t keys = values + total_chars * 2;
    for (uint32_t i = 0; i < n; ++i) {
        const uint32_t vl = rd32(b, 4 + 12 * i + 4), kl = rd32(b, 4 + 12 * i + 8);
        std::u32string text;
        text.reserve(vl);
        for (uint32_t c = 0; c < vl; ++c) {
            const uint16_t u = static_cast<uint16_t>(b[values + 2 * c] | (b[values + 2 * c + 1] << 8));
            text.push_back(static_cast<char32_t>(u));  // the credits are BMP text
        }
        values += static_cast<size_t>(vl) * 2;
        const std::string key(reinterpret_cast<const char*>(b.data() + keys), kl);
        keys += kl;
        CreditsLine l;
        if (text == U"[TBL]" || text.empty()) {
            l.kind = CreditsLine::Kind::Blank;
        } else {
            // 0x44eab6: a key that contains "HEADER" takes the header colours [M: the call is strstr].
            l.kind = key.find("HEADER") != std::string::npos ? CreditsLine::Kind::Header : CreditsLine::Kind::Name;
            l.text = std::move(text);
        }
        out.push_back(std::move(l));
    }
    return true;
}

std::string credits_path(std::string_view language) { return "Data\\Text\\CreditsText_" + std::string(language) + ".DAT"; }

int64_t credits_scroll_target_px(float scroll_rate, int32_t screen_height, double t) {
    return static_cast<int64_t>(std::floor(-static_cast<double>(scroll_rate) * static_cast<double>(screen_height) * t)) + 2;
}

CreditsRect credits_logo_rect(size_t slot, const CreditsLogo& logo, size_t records, int64_t pitch_px, int32_t screen_width,
                              int32_t screen_height, float tex_w, float tex_h) {
    const double sw = screen_width, sh = screen_height;
    CreditsRect r;
    r.w = static_cast<float>(logo.width * sw);
    // 0x44ecdc: the TextureRect is sized (tex_w / 1024, tex_h / 768), then resized to Width keeping that aspect.
    const double aspect = tex_w > 0.0f ? (static_cast<double>(tex_h) / 768.0) / (static_cast<double>(tex_w) / 1024.0) : 0.0;
    r.h = static_cast<float>(logo.height * aspect * sh);
    r.x = slot == 0 ? 0.0f : slot == 1 ? static_cast<float>((sw - r.w) * 0.5) : static_cast<float>(sw - r.w);
    r.y = static_cast<float>(sh * (1.0 + logo.y_offset) + static_cast<double>(records) * static_cast<double>(pitch_px));
    return r;
}

float credits_logo_alpha(float y, float m) {
    auto s = [](float u) { return (3.0f - 2.0f * u) * u * u; };
    if (y < m || y > 1.0f - m) return 0.0f;
    if (m > 0.0f && y < 2.0f * m) return s((y - m) / m);
    if (m > 0.0f && y > 1.0f - 2.0f * m) return 1.0f - s((y - (1.0f - 2.0f * m)) / m);
    return 1.0f;
}

void CreditsRoll::start(std::vector<CreditsLine> lines, const CreditsStyle& style, double now, bool black_backdrop) {
    lines_ = std::move(lines);
    style_ = style;
    t0_ = now;
    black_backdrop_ = black_backdrop;
    moved_px_ = 0;
    last_top_px_ = 0.0f;
    logo_tex_ = {};
    active_ = !lines_.empty();
    drawn_ = 0;
}

bool CreditsRoll::build(HudDrawList& dl, FontCache& fonts, const HudCanvas& cv, double now, const GuiImageResolver* images) {
    drawn_ = 0;
    logos_drawn_ = 0;
    if (!active_) return false;
    const double t = std::max(0.0, now - t0_);
    const auto h = static_cast<int32_t>(cv.screen_height);
    // 0x44e020: one pixel per frame at most, while the roll is behind its target.
    if (moved_px_ < credits_scroll_target_px(style_.scroll_rate, h, t)) ++moved_px_;
    const auto pitch = static_cast<int64_t>(std::floor(style_.spacing * static_cast<float>(h)));
    const auto n = static_cast<int64_t>(lines_.size());
    last_top_px_ = static_cast<float>(h - moved_px_);
    const float sw = static_cast<float>(cv.screen_width), sh = static_cast<float>(h);
    // #458: a logo takes part (drawn, and in the end test) once its texture has resolved; its size is kept from then on,
    // so a page that is reloading for a frame neither moves the end nor drops the logo from it (a logo whose texture
    // never loads is skipped, as retail skips one that fails to load). [port] (docs/DECISIONS.md "Front-end and skirmish
    // choices", credits logos) the textures load through the GUI image pages after the roll starts.
    std::array<GuiImage, 3> now_image{};
    std::array<CreditsRect, 3> rect{};
    float last_top = static_cast<float>(static_cast<int64_t>(h) + std::max<int64_t>(0, (n - 1) * pitch) - moved_px_);
    for (size_t i = 0; i < style_.logos.size(); ++i) {
        const CreditsLogo& logo = style_.logos[i];
        if (logo.name.empty()) continue;
        if (images && *images) now_image[i] = (*images)(logo.name);
        const GuiImage& im = now_image[i];
        if (im.valid && im.size.x > 0.0f && im.size.y > 0.0f) logo_tex_[i] = im.size;
        if (!(logo_tex_[i].x > 0.0f)) continue;  // never resolved yet
        rect[i] = credits_logo_rect(i, logo, lines_.size(), pitch, static_cast<int32_t>(cv.screen_width), h, logo_tex_[i].x, logo_tex_[i].y);
        rect[i].y -= static_cast<float>(moved_px_);
        last_top = std::max(last_top, rect[i].y);
    }
    // 0x44d780: the roll ends once the highest top edge of every record (blanks included) and logo is at or above the top.
    if (last_top <= 0.0f) {
        active_ = false;
        return false;
    }
    if (black_backdrop_) {  // no Credits movie: a black backdrop over the menu
        HudQuad bg;
        const Vec2f a = cv.pixel_to_canvas(0.0f, 0.0f), b = cv.pixel_to_canvas(sw, sh);
        bg.min = {std::min(a.x, b.x), std::min(a.y, b.y)};
        bg.max = {std::max(a.x, b.x), std::max(a.y, b.y)};
        bg.color = {0, 0, 0, 255};
        dl.add(kHudLayers - 1, HudList::Alpha, bg);
    }
    HudTextStyle st;
    st.font = fonts.font(style_.font, style_.font_size, cv.font_height());
    if (st.font < 0) return true;
    st.align = TextAlign::Center;
    st.gradient = true;
    const auto line_h = static_cast<float>(fonts.metrics(st.font).line_height);
    for (size_t i = 0; i < lines_.size(); ++i) {
        const CreditsLine& l = lines_[i];
        if (l.kind == CreditsLine::Kind::Blank) continue;
        // The top edge, pixels from the top of the screen; add_text anchors on the line's vertical centre.
        const float top = static_cast<float>(static_cast<int64_t>(h) + static_cast<int64_t>(i) * pitch - moved_px_);
        if (top + line_h < 0.0f || top > sh) continue;
        const bool header = l.kind == CreditsLine::Kind::Header;
        st.color = header ? style_.header_top : style_.top;
        st.color_bottom = header ? style_.header_bottom : style_.bottom;
        add_text(dl, kHudLayers - 1, HudList::Text, fonts, cv, l.text, cv.pixel_to_canvas(sw * 0.5f, top + line_h * 0.5f), st);
        ++drawn_;
    }
    for (size_t i = 0; i < rect.size(); ++i) {  // #458: faded by Credits_Margin on their top edge
        const GuiImage& im = now_image[i];
        const CreditsRect& r = rect[i];
        const float a = credits_logo_alpha(r.y / sh, style_.margin);  // 0 below 1 - m: nothing under the screen
        if (!im.valid || !(r.w > 0.0f) || !(a > 0.0f)) continue;
        HudQuad q;
        const Vec2f p0 = cv.pixel_to_canvas(r.x, r.y), p1 = cv.pixel_to_canvas(r.x + r.w, r.y + r.h);
        q.min = {std::min(p0.x, p1.x), std::min(p0.y, p1.y)};
        q.max = {std::max(p0.x, p1.x), std::max(p0.y, p1.y)};
        q.uv0 = im.uv0;
        q.uv1 = im.uv1;
        q.page = im.page;
        q.color = {255, 255, 255, static_cast<uint8_t>(std::lround(std::clamp(a, 0.0f, 1.0f) * 255.0f))};
        dl.add(kHudLayers - 1, HudList::Alpha, q);
        ++logos_drawn_;
    }
    return true;
}

}  // namespace alamo::ui
