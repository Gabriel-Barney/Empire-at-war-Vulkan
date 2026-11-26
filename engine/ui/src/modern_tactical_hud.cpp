#include "alamo/ui/modern_tactical_hud.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "alamo/core/ascii.hpp"
#include "alamo/ui/gui_menus.hpp"
#include "alamo/ui/hud_font.hpp"
#include "alamo/ui/orders.hpp"
#include "ui_strings.hpp"

namespace alamo::ui {

namespace {

constexpr Rgba8 kBarBack{0, 0, 0, 170};
constexpr Rgba8 kPausedBar{196, 176, 8, 245};  // the Classic pause banner's yellow bar
// By TacticalHudFrame::faction_alt (GameUi::set_faction): 0 the good side (Rebel), 1 any other (Empire, Underworld).
constexpr Rgba8 kAccent[2] = {{226, 120, 48, 255}, {92, 156, 230, 255}};
constexpr uint32_t kPanelLayer = 3, kControlLayer = 4, kBannerLayer = 11;  // the banner over the panels, as Classic's
constexpr float kMargin = 8.0f;        // canvas units from the screen edge
constexpr float kPad = 8.0f;           // inside a panel
constexpr float kDeckHeight = 120.0f;  // canvas units
// c_button00..05: attack, attack-move, move, waypoint, full stop, guard (HudOrderButtons), their port labels and hotkeys.
constexpr const char32_t* kOrderLabels[kOrderButtonCount] = {U"Attack", U"Attack Move", U"Move", U"Waypoint", U"Stop", U"Guard"};
constexpr uint16_t kOrderSlots[kOrderButtonCount] = {hk::kAttack, hk::kAttackMove, hk::kMove, hk::kWaypoint, hk::kStop, hk::kGuard};

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

// A bar `fraction` full from the left: solid (hull) or striped (shields: 2-unit stripes every 4 units), so the two are
// told apart without colour.
void add_bar(HudDrawList& dl, uint32_t layer, Vec2f c, Vec2f size, float fraction, Rgba8 color, bool striped) {
    const float x0 = c.x - 0.5f * size.x, y0 = c.y - 0.5f * size.y, y1 = c.y + 0.5f * size.y;
    add_rect(dl, layer, x0, y0, x0 + size.x, y1, kBarBack);
    const float end = x0 + size.x * std::clamp(fraction, 0.0f, 1.0f);
    if (!striped) {
        if (end > x0) add_rect(dl, layer, x0, y0, end, y1, color);
        return;
    }
    for (float x = x0; x < end; x += 4.0f) add_rect(dl, layer, x, y0, std::min(x + 2.0f, end), y1, color);
}

// The card's name line: the type's Text_ID text, else [port] its name with '_' as spaces.
std::u32string type_label(const HudTypeInfo* t, const TextDatabase* text) {
    std::u32string out;
    if (!t) return out;
    if (const std::u16string* s = text && !t->text_id.empty() ? text->find(t->text_id) : nullptr) return utf16_to_utf32(*s);
    for (char ch : t->name) out.push_back(ch == '_' ? U' ' : static_cast<char32_t>(static_cast<unsigned char>(ch)));
    return out;
}

}  // namespace

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

std::optional<std::u32string> radar_alert_text(const RadarEvent& e, const TextDatabase* text) {
    if (e.marker && e.type.empty()) return text_or_port(text, nullptr, U"Base under attack");
    return std::nullopt;  // a story blip (Lua Add_Radar_Blip), a click, a beacon, any other event
}

std::string_view hud_style_name(HudStyle s) { return s == HudStyle::Modern ? "modern" : "classic"; }

std::optional<HudStyle> parse_hud_style(std::string_view s) {
    if (ascii_iequals(s, "classic")) return HudStyle::Classic;
    if (ascii_iequals(s, "modern")) return HudStyle::Modern;
    return std::nullopt;
}

std::string_view hud_colours_name(HudColours c) {
    return c == HudColours::ColourBlind ? "colourblind" : c == HudColours::HighContrast ? "contrast" : "faction";
}

std::optional<HudColours> parse_hud_colours(std::string_view s) {
    if (ascii_iequals(s, "faction")) return HudColours::Faction;
    if (ascii_iequals(s, "colourblind")) return HudColours::ColourBlind;
    if (ascii_iequals(s, "contrast")) return HudColours::HighContrast;
    return std::nullopt;
}

void ModernTacticalHud::init(const ComponentTable& table, const TextureAtlas& atlas, FontCache& fonts, const TextDatabase* text) {
    table_ = &table;
    atlas_ = &atlas;
    fonts_ = &fonts;
    text_ = text;
    option_ = table.find("b_option_t");
    pause_ = table.find("b_play_pause_t");
    fast_forward_ = table.find("b_fast_forward_t");
    resume_ = table.find("attack_button");
    for (uint32_t i = 0; i < kOrderButtonCount; ++i) {
        char n[16];
        std::snprintf(n, sizeof n, "c_button%02d", i);
        order_[i] = table.find(n);
    }
    if (const int32_t c = table.find("Text_Credits_tactical"); c >= 0 && table[static_cast<size_t>(c)].has_text_color)
        credits_color_ = table[static_cast<size_t>(c)].text_color;
    set_options(options_);  // the palette from the options' opacity
}

void ModernTacticalHud::resize(uint32_t width, uint32_t height) { resize_canvas(canvas_, nullptr, width, height); }

void ModernTacticalHud::set_text_scale(float s) {
    text_scale_ = std::clamp(s > 0.0f ? s : kModernTextScaleDefault, kModernTextScaleMin, kModernTextScaleMax);
    options_.text_scale = static_cast<int32_t>(std::lround(text_scale_ * 100.0f));
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

// [port: Gabriel] The Modern look (#520 design canvas "Modern HUD redesign" v2; #590 colour choices): dark translucent
// panels (the panel opacity) with an edge in the local faction's colour; colour-blind safe = the Okabe-Ito palette (blue
// accent, bluish green / orange / vermillion hull); high contrast = near-opaque black panels, white text, yellow accent.
void ModernTacticalHud::set_options(const ModernHudOptions& o) {
    options_ = clamp_options(o);
    text_scale_ = static_cast<float>(options_.text_scale) / 100.0f;
    const auto alpha = static_cast<uint8_t>(std::lround(2.55f * static_cast<float>(options_.panel_opacity)));
    Palette c;
    c.back = {10, 14, 22, alpha};
    c.button = {24, 32, 46, static_cast<uint8_t>(std::min(255, alpha + 30))};
    switch (options_.colours) {
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
    colours_ = c;
}

Rgba8 ModernTacticalHud::hull_colour(float health) const {
    return health > kModernHullWarn ? colours_.hull_ok : health > kModernHullCritical ? colours_.hull_warn : colours_.hull_bad;
}

Rgba8 ModernTacticalHud::accent(uint8_t alpha) const {
    Rgba8 c = colours_.faction_accent ? kAccent[faction_alt_ ? 1 : 0] : colours_.accent;
    c.a = alpha;
    return c;
}

void ModernTacticalHud::hit(int32_t id, uint32_t layer, Vec2f center, Vec2f size) {
    hits_.push_back(make_hit_box(id, layer, center, size));
}

void ModernTacticalHud::build(const TacticalHudFrame& frame, std::span<const HudUnit> units, std::span<const HudTypeInfo> types) {
    dl_.clear();
    hits_.clear();
    groups_.clear();
    wing_ids_.clear();
    for (bool& l : order_live_) l = false;
    stats_ = {};
    paused_ = frame.paused;
    faction_alt_ = frame.faction_alt;
    jump_ok_ = false;  // before the early return: a hidden HUD has no Jump
    if (!table_ || !fonts_ || frame.world.interface_hidden || font() < 0) return;
    build_deck(frame, units, types);
    build_status(frame);
    build_controls(frame);
    build_alerts(frame);
    build_objectives(frame, controls_bottom_ - kPad);
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
    if (!stats_.pop_text.empty()) rows.push_back({text_or_port(text_, nullptr, U"Population"), stats_.pop_text, colours_.text});
    if (!stats_.tech_text.empty()) rows.push_back({{}, stats_.tech_text, colours_.text});
    if (rows.empty()) return;
    HudTextStyle value_style, label_style;
    value_style.font = label_style.font = font();
    label_style.color = colours_.label;
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
    frame_rect(dl_, kPanelLayer, center, size, colours_.back, accent(200));
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
    controls_bottom_ = y - 0.5f * h;
    float right = hw - kMargin;  // laid out right to left
    HudTextStyle st;
    st.font = font();
    st.color = colours_.text;
    st.align = TextAlign::Center;
    auto button = [&](int32_t id, std::u32string_view label, bool selected) {
        const float w = std::max(h, measure_text_block(*fonts_, canvas_, label, 0.0f, st).width + 2.0f * kPad);
        const Vec2f c{right - 0.5f * w, y};
        frame_rect(dl_, kControlLayer, c, {w, h}, selected ? accent(220) : colours_.button, hover_ == id ? colours_.text : accent(200));
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
    frame_rect(dl_, kControlLayer, c, size, colours_.button, hover_ == kModernStyleSwitchHit ? colours_.text : accent(200));
    const float modern_x = c.x - 0.5f * size.x + 0.5f * modern_w, classic_x = c.x + 0.5f * size.x - 0.5f * classic_w;
    add_rect(dl_, kControlLayer, modern_x - 0.5f * modern_w + 2.0f, y - 0.5f * h + 2.0f, modern_x + 0.5f * modern_w - 2.0f,
             y + 0.5f * h - 2.0f, accent(220));
    add_text(dl_, kControlLayer, HudList::Text, *fonts_, canvas_, modern, {modern_x, y}, st);
    HudTextStyle dim = st;
    dim.color = colours_.label;
    add_text(dl_, kControlLayer, HudList::Text, *fonts_, canvas_, classic, {classic_x, y}, dim);
    hit(kModernStyleSwitchHit, kControlLayer, c, size);
    stats_.style_switch = true;
}

void ModernTacticalHud::build_deck(const TacticalHudFrame& f, std::span<const HudUnit> units, std::span<const HudTypeInfo> types) {
    const Vec2f size{canvas_.width - 2.0f * kMargin, kDeckHeight};
    const Vec2f center{0.0f, -0.5f * canvas_.height + kMargin + 0.5f * size.y};
    frame_rect(dl_, kPanelLayer, center, size, colours_.back, accent(200));
    hit(kModernPanelHit, kPanelLayer, center, size);
    stats_.deck = true;
    // Selection and orders only with an own unit selected (#520: "abilities and orders appear only when I have a selection").
    const bool own = std::any_of(units.begin(), units.end(),
                                 [&](const HudUnit& u) { return u.selected && u.owner == f.world.local_player; });
    if (!own) return;
    const float left = center.x - 0.5f * size.x + kPad, right = center.x + 0.5f * size.x - kPad;
    const float top = center.y + 0.5f * size.y - kPad, height = size.y - 2.0f * kPad;
    const float orders_w = build_orders(f, {right, top}, height);
    build_selection(f, units, types, {left, top}, right - orders_w - 2.0f * kPad - left, height);
}

// The selection section: a header row (wing buttons, the clear-selection button) over the unit cards.
void ModernTacticalHud::build_selection(const TacticalHudFrame& f, std::span<const HudUnit> units,
                                        std::span<const HudTypeInfo> types, Vec2f top_left, float width, float height) {
    const float line = text_height_units();
    const float header = line * 1.6f;
    HudTextStyle st;
    st.font = font();
    st.color = colours_.text;
    HudTextStyle centred = st;
    centred.align = TextAlign::Center;
    // Header: the wings (Classic's hit ids and members), then Clear at the right end.
    float x = top_left.x;
    const float hy = top_left.y - 0.5f * header;
    for (size_t w = 0; w < f.wings.size() && w < kMaxWings; ++w) {
        const HudWing& wing = f.wings[w];
        wing_ids_.push_back(wing.squadrons > 0 ? wing.ids : std::vector<uint32_t>{});
        if (wing.squadrons == 0 || wing.ids.empty()) continue;
        char n[16];
        std::snprintf(n, sizeof n, " %u", wing.squadrons);
        const std::u32string label = wing.label + ascii32(n);
        const float hint_w = wing.hotkey.empty() || !options_.key_hints ? 0.0f : measure_text_block(*fonts_, canvas_, wing.hotkey, 0.0f, st).width + kPad;
        const float w_units = measure_text_block(*fonts_, canvas_, label, 0.0f, st).width + hint_w + 2.0f * kPad;
        const Vec2f c{x + 0.5f * w_units, hy};
        const int32_t id = kWingHitBase + static_cast<int32_t>(w);
        frame_rect(dl_, kControlLayer, c, {w_units, header}, wing.selected ? accent(220) : colours_.button,
                   hover_ == id ? colours_.text : accent(200));
        add_text(dl_, kControlLayer, HudList::Text, *fonts_, canvas_, label, {c.x - 0.5f * hint_w, c.y}, centred);
        if (!wing.hotkey.empty() && options_.key_hints) {  // the wing's key hint (HudWing::hotkey), as the Classic button draws it
            HudTextStyle ks = st;
            ks.color = colours_.label;
            ks.align = TextAlign::Right;
            add_text(dl_, kControlLayer, HudList::Text, *fonts_, canvas_, wing.hotkey, {c.x + 0.5f * w_units - kPad, c.y}, ks);
            ++stats_.key_badges;
        }
        hit(id, kControlLayer, c, {w_units, header});
        x += w_units + 0.5f * kPad;
        ++stats_.wings;
    }
    {
        const std::u32string clear = text_or_port(text_, nullptr, U"Clear");
        const float cw = measure_text_block(*fonts_, canvas_, clear, 0.0f, centred).width + 2.0f * kPad;
        const Vec2f c{top_left.x + width - 0.5f * cw, hy};
        frame_rect(dl_, kControlLayer, c, {cw, header}, colours_.button, hover_ == kModernClearHit ? colours_.text : accent(200));
        add_text(dl_, kControlLayer, HudList::Text, *fonts_, canvas_, clear, c, centred);
        hit(kModernClearHit, kControlLayer, c, {cw, header});
        stats_.clear_button = true;
    }
    // Cards: the Classic grouping; with more groups than fit, the last card is "+N" (every unit not shown).
    const float card_h = height - header - 0.5f * kPad, card_w = std::max(card_h * 0.8f, 6.0f * line);
    const float gap = 0.5f * kPad;
    const auto fit = static_cast<size_t>(std::max(1.0f, std::floor((width + gap) / (card_w + gap))));
    if (f.fleet_groups_on) {
        groups_ = group_selection_fleets(units, f.fleet_groups, fit);
    } else {
        groups_ = group_selection(units);  // the Classic grouping and its cap
        fold_selection_overflow(groups_, fit);
    }
    const float cy = top_left.y - header - 0.5f * kPad - 0.5f * card_h;
    for (size_t i = 0; i < groups_.size(); ++i) {
        const SelectedGroup& g = groups_[i];
        const Vec2f c{top_left.x + 0.5f * card_w + static_cast<float>(i) * (card_w + gap), cy};
        const int32_t id = kModernCardHitBase + static_cast<int32_t>(i);
        frame_rect(dl_, kControlLayer, c, {card_w, card_h}, colours_.button, hover_ == id ? colours_.text : accent(160));
        hit(id, kControlLayer, c, {card_w, card_h});
        ++stats_.cards;
        char n[24];
        if (g.overflow) {
            std::snprintf(n, sizeof n, "+%zu", g.ids.size());
            add_text(dl_, kControlLayer, HudList::Text, *fonts_, canvas_, ascii32(n), c, centred);
            stats_.overflow_card = true;
            continue;
        }
        const HudTypeInfo* t = g.type < types.size() ? &types[g.type] : nullptr;
        const float icon = std::max(8.0f, std::min(card_w - 2.0f * kPad, card_h - 2.0f * line - 3.0f * kPad));
        const Vec2f ic{c.x, c.y + 0.5f * card_h - kPad - 0.5f * icon};
        if (t && !t->icon_name.empty() && atlas_)
            add_atlas_quad(dl_, kControlLayer, HudList::Alpha, *atlas_, t->icon_name, ic, {icon, icon}, 1.0f, Rgba8{}, {icon, icon});
        if (g.ids.size() > 1) {
            std::snprintf(n, sizeof n, "x%zu", g.ids.size());
            HudTextStyle ns = st;
            ns.align = TextAlign::Right;
            ns.outline = true;
            add_text(dl_, kControlLayer, HudList::Text, *fonts_, canvas_, ascii32(n),
                     {c.x + 0.5f * card_w - 0.5f * kPad, ic.y + 0.5f * icon - 0.5f * line}, ns);
        }
        if (const std::u32string name = type_label(t, text_); !name.empty()) {
            HudTextStyle ns = centred;
            ns.width_scale = fit_width_scale(measure_text_block(*fonts_, canvas_, name, 0.0f, ns).width, card_w - kPad);
            add_text(dl_, kControlLayer, HudList::Text, *fonts_, canvas_, name, {c.x, ic.y - 0.5f * icon - 0.6f * line}, ns);
        }
        const Vec2f bar{card_w - 2.0f * kPad, 4.0f};
        const float by = c.y - 0.5f * card_h + kPad + (g.shield >= 0.0f ? 8.0f : 2.0f);
        add_bar(dl_, kControlLayer, {c.x, by}, bar, g.health, hull_colour(g.health), false);
        if (g.shield >= 0.0f) add_bar(dl_, kControlLayer, {c.x, by - 6.0f}, bar, g.shield, colours_.shield, true);
    }
}

// A key badge (the slot's live binding) with its top-right corner at `top_right`.
void ModernTacticalHud::key_badge(uint16_t slot, Vec2f top_right, uint32_t layer, bool always) {
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
    ++stats_.key_badges;
}

// The order buttons (2 rows of 3, the right end of the deck) and the cohesion line under them.
float ModernTacticalHud::build_orders(const TacticalHudFrame& f, Vec2f top_right, float height) {
    const float line = text_height_units();
    const bool cohesion = !f.cohesion_text.empty();
    const float row_h = 0.5f * (height - (cohesion ? line * 1.4f : 0.0f) - 0.5f * kPad);
    const float col_w = line * 7.5f, gap = 0.5f * kPad;
    const float width = 3.0f * col_w + 2.0f * gap;
    HudTextStyle st;
    st.font = font();
    st.align = TextAlign::Center;
    for (uint32_t i = 0; i < kOrderButtonCount; ++i) {
        if (order_[i] < 0) continue;
        const bool live = order_button_enabled(i, f.order_buttons);  // the Classic rule (HUD_GAPS §5)
        order_live_[i] = live;
        const float cx = top_right.x - width + 0.5f * col_w + static_cast<float>(i % 3) * (col_w + gap);
        const float cy = top_right.y - 0.5f * row_h - static_cast<float>(i / 3) * (row_h + gap);
        const Vec2f c{cx, cy}, size{col_w, row_h};
        if (live) {
            frame_rect(dl_, kControlLayer, c, size, colours_.button, hover_ == order_[i] ? colours_.text : accent(200));
        } else {
            add_rect(dl_, kControlLayer, c.x - 0.5f * size.x, c.y - 0.5f * size.y, c.x + 0.5f * size.x, c.y + 0.5f * size.y,
                     Rgba8{16, 20, 28, 160});
            dashed_rect(dl_, kControlLayer, c, size, colours_.disabled);
        }
        st.color = live ? colours_.text : colours_.disabled;
        if (options_.order_labels) {  // off: the key badge alone names the order (drawn even with key hints off)
            const std::u32string label = text_or_port(text_, nullptr, kOrderLabels[i]);
            st.width_scale = fit_width_scale(measure_text_block(*fonts_, canvas_, label, 0.0f, st).width, col_w - kPad);
            add_text(dl_, kControlLayer, HudList::Text, *fonts_, canvas_, label, {c.x, c.y - 0.2f * row_h}, st);
        }
        key_badge(kOrderSlots[i], {c.x + 0.5f * size.x - 2.0f, c.y + 0.5f * size.y - 2.0f}, kControlLayer, !options_.order_labels);
        hit(order_[i], kControlLayer, c, size);
        ++stats_.orders;
        if (live) ++stats_.orders_live;
    }
    if (cohesion) {  // [port: Gabriel 2026-11-13] fleet cohesion (Loose / Tight), toggled by its hotkey
        HudTextStyle cs;
        cs.font = font();
        cs.color = colours_.label;
        const float y = top_right.y - height + 0.6f * line;
        add_text(dl_, kControlLayer, HudList::Text, *fonts_, canvas_, f.cohesion_text, {top_right.x - width, y}, cs);
        key_badge(hk::kFleetCohesion, {top_right.x, y + 0.6f * line}, kControlLayer);
        stats_.cohesion = true;
    }
    return width;
}

// The alert strip: the newest alert at the top centre (in the controls row), the count of the others, and Jump.
void ModernTacticalHud::build_alerts(const TacticalHudFrame& f) {
    if (f.alerts.empty()) return;
    const HudAlert& a = f.alerts.back();
    const float h = text_height_units() * 2.0f;
    const float y = 0.5f * canvas_.height - kMargin - 0.5f * h;
    HudTextStyle st;
    st.font = font();
    st.color = colours_.text;
    std::u32string more;
    if (f.alerts.size() > 1) {
        char n[16];
        std::snprintf(n, sizeof n, "  +%zu", f.alerts.size() - 1);
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
        stats_.jump = true;
    }
    stats_.alert_text = a.text;
    stats_.alerts_more = static_cast<uint32_t>(f.alerts.size() - 1);
}

// The objectives panel: the Classic entries with their state as a shape; folded to its header while objectives_hidden.
void ModernTacticalHud::build_objectives(const TacticalHudFrame& f, float top) {
    if (f.objectives.empty()) return;
    const float line = text_height_units() * 1.4f;
    const float width = std::min(0.3f * canvas_.width, 22.0f * text_height_units());
    const float right = 0.5f * canvas_.width - kMargin;
    HudTextStyle st;
    st.font = font();
    st.color = colours_.text;
    std::vector<std::pair<std::vector<std::u32string>, HudObjective::State>> entries;
    const float mark = 0.6f * text_height_units();
    const float wrap_px = (width - 3.0f * kPad - mark) / canvas_.units_per_pixel_x;
    size_t lines = 0;
    if (!f.objectives_hidden)
        for (const HudObjective& o : f.objectives) {
            const std::u32string t = hud_objective_text(text_, o);
            stats_.objective_texts.push_back(t);
            entries.push_back({wrap_text(*fonts_, st.font, t, wrap_px), o.state});
            lines += entries.back().first.size();
        }
    const std::u32string header = f.objectives_header.empty() ? text_or_port(text_, "TEXT_TEST_OBJECTIVE_HEADER", U"Mission Objectives")
                                                              : f.objectives_header;
    const Vec2f size{width, line * static_cast<float>(1 + lines) + kPad};
    const Vec2f c{right - 0.5f * width, top - 0.5f * size.y};
    frame_rect(dl_, kPanelLayer, c, size, colours_.back, accent(200));
    hit(kModernPanelHit, kPanelLayer, c, size);
    float y = top - 0.5f * kPad - 0.5f * line;
    const float left = c.x - 0.5f * width + kPad;
    HudTextStyle hs = st;
    hs.color = colours_.label;
    add_text(dl_, kPanelLayer, HudList::Text, *fonts_, canvas_, header, {left, y}, hs);
    key_badge(hk::kHolocron, {c.x + 0.5f * width - 2.0f, top - 2.0f}, kPanelLayer);
    for (const auto& [wrapped, state] : entries) {
        y -= line;
        const Vec2f mc{left + 0.5f * mark, y};
        switch (state) {  // open: an outline; done: filled; failed: a cross
            case HudObjective::State::Complete:
                add_rect(dl_, kPanelLayer, mc.x - 0.5f * mark, mc.y - 0.5f * mark, mc.x + 0.5f * mark, mc.y + 0.5f * mark, accent(255));
                break;
            case HudObjective::State::Failed:
                for (const float s : {1.0f, -1.0f}) {
                    HudQuad q;
                    q.has_corners = true;
                    const float d = 0.5f * mark, t = 1.0f;
                    q.corners = {Vec2f{mc.x - d, mc.y + s * d}, Vec2f{mc.x - d + t, mc.y + s * d}, Vec2f{mc.x + d, mc.y - s * d},
                                 Vec2f{mc.x + d - t, mc.y - s * d}};
                    q.color = colours_.hull_bad;  // the hull bar's critical colour
                    dl_.add(kPanelLayer, HudList::Alpha, q);
                }
                break;
            default: frame_rect(dl_, kPanelLayer, mc, {mark, mark}, Rgba8{0, 0, 0, 0}, colours_.text); break;
        }
        bool first = true;
        for (const std::u32string& w : wrapped) {
            if (!first) y -= line;
            HudTextStyle ts = st;
            if (state != HudObjective::State::Active) ts.color = colours_.label;
            add_text(dl_, kPanelLayer, HudList::Text, *fonts_, canvas_, w, {left + mark + kPad, y}, ts);
            first = false;
        }
    }
    stats_.objectives_panel = true;
    stats_.objectives_folded = f.objectives_hidden;
}

bool ModernTacticalHud::jump(std::vector<HudAction>& actions) const {
    if (!jump_ok_) return false;
    HudAction h;
    h.kind = HudAction::Kind::RadarCameraJump;
    h.component = kModernJumpHit;
    h.x = jump_at_.x;
    h.y = jump_at_.y;
    actions.push_back(std::move(h));
    return true;
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
        frame_rect(dl_, kBannerLayer, c, size, hover_ == resume_ ? accent(240) : colours_.button, accent(230));
        HudTextStyle bs = st;
        bs.color = colours_.text;
        add_text(dl_, kBannerLayer, HudList::Text, *fonts_, canvas_, resume, c, bs);
        hit(resume_, kBannerLayer, c, size);
    }
    stats_.pause_banner = true;
}

std::vector<uint32_t> ModernTacticalHud::selection_ids() const {
    std::vector<uint32_t> ids;
    for (const SelectedGroup& g : groups_) ids.insert(ids.end(), g.ids.begin(), g.ids.end());
    std::sort(ids.begin(), ids.end());
    return ids;
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
        // Cards: the Classic portrait click (TacticalHud::update_input "Selection portraits").
        if (e.component >= kModernCardHitBase && e.component - kModernCardHitBase < static_cast<int32_t>(groups_.size())) {
            if (e.button != Button::Left) continue;
            const SelectedGroup& g = groups_[static_cast<size_t>(e.component - kModernCardHitBase)];
            out.push_back(commands::ev::select(portrait_select_ids(g, selection_ids(), shift, ctrl)));
            continue;
        }
        if (e.component == kModernJumpHit) {
            if (e.button == Button::Left && actions) jump(*actions);
            continue;
        }
        if (e.component == kModernClearHit) {
            if (e.button == Button::Left) out.push_back(commands::ev::select({}));
            continue;
        }
        if (e.component >= kModernHitBase) continue;  // a panel's back
        // Wing buttons: the Classic ids and action (TacticalHud::update_input "squadron wing buttons").
        if (e.component >= kWingHitBase && e.component < kFleetTabHitBase) {
            const auto w = static_cast<size_t>(e.component - kWingHitBase);
            if (e.button == Button::Left && w < wing_ids_.size() && !wing_ids_[w].empty())
                act(wing_select_action(static_cast<int32_t>(w), wing_ids_[w], shift, ctrl));
            continue;
        }
        // A disabled order takes no click (the Classic HudButtons swallows it).
        bool dead_order = false;
        for (uint32_t i = 0; i < kOrderButtonCount; ++i) dead_order = dead_order || (order_[i] == e.component && !order_live_[i]);
        if (dead_order) continue;
        const std::string& name = (*table_)[static_cast<size_t>(e.component)].name;
        // The Classic clicks of the shared controls: Resume Game while paused, pause through the command stream, the
        // other buttons (menu, fast forward, the orders) as a Button action by component name.
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
