#include "alamo/ui/modern_tactical_hud.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "alamo/core/ascii.hpp"
#include "alamo/ui/gui_menus.hpp"
#include "alamo/ui/hud_font.hpp"
#include "alamo/ui/orders.hpp"
#include "alamo/ui/picking.hpp"
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

std::u32string count_text(uint32_t n) {
    char b[16];
    std::snprintf(b, sizeof b, "%u", n);
    return ascii32(b);
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
    special_button_.assign(48, -1);  // as TacticalHud::init
    for (int i = 0; i < 48; ++i) {
        char n[24];
        std::snprintf(n, sizeof n, "special_button_%02d", i);
        special_button_[static_cast<size_t>(i)] = table.find(n);
        std::snprintf(n, sizeof n, "s_select_%02d", i);
        if (panel_slots_ == static_cast<size_t>(i) && table.find(n) >= 0) ++panel_slots_;
    }
    radar_ = table.find("radar");  // #591
    retreat_ = table.find("b_retreat");  // #587, as TacticalHud::init
    special_weapon_ = table.find("b_special_weapon");
    reinforce_button_ = table.find("b_reinforcement");
    beacon_ = table.find("b_special_weapon2");  // TEXT_PLACE_BEACON (b_beacon_t: the same action, the other skin's button)
    if (beacon_ < 0) beacon_ = table.find("b_beacon_t");
    auto numbered = [&](const char* fmt, int n, std::vector<int32_t>& out) {
        out.clear();
        for (int i = 0; i < n; ++i) {
            char nm[24];
            std::snprintf(nm, sizeof nm, fmt, i);
            out.push_back(table.find(nm));
        }
    };
    numbered("Build_%02d", kTacticalBuyButtons, buy_);
    numbered("tqueue%02d", kTacticalQueueSlots, queue_);
    reinforce_.clear();
    for (int r = 0; r < kReinforceRows; ++r)
        for (int c = 0; c < kReinforceColumns; ++c) {
            char nm[24];
            std::snprintf(nm, sizeof nm, "r_%02d%02d", r, c);
            reinforce_.push_back(table.find(nm));
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
    abilities_.clear();
    dead_actions_.clear();
    buy_slots_.clear();
    queue_slots_.clear();
    reinforce_slots_.clear();
    fleet_shortcuts_.clear();
    fleet_tab_ids_.clear();
    fleet_tab_group_.clear();
    builder_ = frame.builder;
    station_ = commands::kNoObject;
    map_view_ = {};
    radar_models_.clear();
    ground_z_ = frame.ground_z;
    // Classic's radar move click needs its selection groups (TacticalHud::build: its grouping and slot count).
    select_groups_ = !(frame.fleet_groups_on ? group_selection_fleets(units, frame.fleet_groups, panel_slots_) : group_selection(units)).empty();
    wing_ids_.clear();
    for (bool& l : order_live_) l = false;
    stats_ = {};
    paused_ = frame.paused;
    faction_alt_ = frame.faction_alt;
    jump_ok_ = false;  // before the early return: a hidden HUD has no Jump
    if (!table_ || !fonts_ || frame.world.interface_hidden || font() < 0) return;
    build_deck(frame, units, types);
    build_map(frame, units, types);
    build_target_card(frame, units, types);
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
    // Selection and orders only with an own unit selected (#520: "abilities and orders appear only when I have a selection").
    const bool own = std::any_of(units.begin(), units.end(),
                                 [&](const HudUnit& u) { return u.selected && u.owner == f.world.local_player; });
    const bool producing = f.builder != commands::kNoObject && !f.build_options.empty();
    const bool reinforcing = f.reinforce_list_open && f.providers.reinforcements;
    const bool idle = !own && !producing && !reinforcing;
    const float height = kDeckHeight - 2.0f * kPad;
    // #591 the map sits at the deck's left end (right with the map side option): the deck fills the rest of the row.
    const float region_left = -0.5f * canvas_.width + kMargin + (options_.map_right ? 0.0f : map_side() + kMargin);
    const float region_right = 0.5f * canvas_.width - kMargin - (options_.map_right ? map_side() + kMargin : 0.0f);
    float width = region_right - region_left;
    if (idle && options_.contextual) {  // #587 shrunk to the battle actions, the shortcuts and the queue
        float w = build_actions(f, {}, height, false) + 2.0f * kPad;
        for (const float part : {build_shortcuts(f, units, types, {}, height, false), build_queue(f, {}, height, 1e9f, false, false)})
            if (part > 0.0f) w += part + 2.0f * kPad;
        width = std::min(width, w);
    }
    const Vec2f size{width, kDeckHeight};
    // Centred on the screen when it fits beside the map (a shrunk deck), else filling the row.
    const float deck_x = std::clamp(0.0f, region_left + 0.5f * width, region_right - 0.5f * width);
    const Vec2f center{deck_x, -0.5f * canvas_.height + kMargin + 0.5f * size.y};
    frame_rect(dl_, kPanelLayer, center, size, colours_.back, accent(200));
    hit(kModernPanelHit, kPanelLayer, center, size);
    stats_.deck = true;
    stats_.deck_width = width;
    float left = center.x - 0.5f * size.x + kPad;
    const float right = center.x + 0.5f * size.x - kPad, top = center.y + 0.5f * size.y - kPad;
    if (const float aw = build_actions(f, {left, top}, height, true); aw > 0.0f) left += aw + 2.0f * kPad;
    if (idle) {  // the shortcuts, then the whole build queue (Classic shows it whatever is selected)
        if (const float sw = build_shortcuts(f, units, types, {left, top}, height, true); sw > 0.0f) left += sw + 2.0f * kPad;
        build_queue(f, {left, top}, height, right - left, false, true);
        return;
    }
    float section_right = right;
    if (own) {
        const float orders_w = build_orders(f, {right, top}, height);
        section_right = right - orders_w - 2.0f * kPad;
        const float abilities_w = build_abilities(f, units, {section_right, top}, height, 0.3f * (section_right - left));
        if (abilities_w > 0.0f) section_right -= abilities_w + 2.0f * kPad;
    }
    // The left section: the reinforcement list while open, else production with a builder, else the selection cards.
    if (reinforcing && build_reinforcements(f, {left, top}, height, section_right - left) > 0.0f) return;
    if (producing && build_production(f, units, types, {left, top}, height, section_right - left) > 0.0f) return;
    if (own) build_selection(f, units, types, {left, top}, section_right - left, height);
}

// #587 a deck button: its back (the selected look: filled, with a corner notch; disabled: a dashed outline), the icon or
// text, a countdown ring, a count badge and the key badge.
void ModernTacticalHud::tile(const Tile& t) {
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

// #587 the battle actions, two rows of two at the deck's left end, always: Retreat (its countdown ring), Reinforce (the
// pad's entry count; the selected look while a drop point is picked), the special weapon / bombing run (its cooldown ring;
// absent while the feed hides it) and the beacon. The feed's states are Classic's (HudCommandButtons, HudBombingRunButton);
// clicks are Classic's (retreat command, else the Button action by name).
float ModernTacticalHud::build_actions(const TacticalHudFrame& f, Vec2f top_left, float height, bool draw) {
    const HudCommandButtons& cb = f.command_buttons;
    const HudBombingRunButton& bomb = f.bombing_run;
    std::vector<Tile> tiles;
    // Classic's states (apply_command_buttons / apply_bombing_run_button), with the Lua overrides of its buttons.
    auto add = [&](int32_t id, const char32_t* label, bool hidden, bool disabled, bool selected, float ring, std::u32string badge,
                   int32_t key) {
        if (id < 0) return;
        const ButtonRuntime* r = script_ ? script_->find_state(static_cast<uint32_t>(id)) : nullptr;
        if (script_hidden(r ? r->script_visible : int8_t{-1}, hidden)) return;
        Tile t;
        t.id = id;
        t.text = text_or_port(text_, nullptr, label);
        t.enabled = !script_disabled(r ? r->script_enabled : int8_t{-1}, disabled);
        t.selected = selected;
        t.ring = ring;
        t.badge = std::move(badge);
        t.hotkey = key;
        tiles.push_back(std::move(t));
    };
    const bool locked = cb.controls_locked;
    if (cb.present) {
        add(retreat_, U"Retreat", false, locked || !cb.retreat_enabled, false, cb.retreat_dial, {}, hk::kRetreat);
        add(reinforce_button_, U"Reinforce", false, locked || !cb.reinforce_enabled, cb.reinforce_selected, -1.0f,
            cb.reinforce_count > 0 ? count_text(cb.reinforce_count) : std::u32string(), hk::kReinforce);
    }
    // No hotkey slot fires the special weapon (the original binds none), so it has no key badge.
    if (bomb.present) add(special_weapon_, f.world.land ? U"Bombing Run" : U"Special Weapon", bomb.hidden, bomb.disabled, false, bomb.dial, {}, -1);
    if (cb.present) add(beacon_, U"Beacon", false, locked || !cb.beacon_enabled, cb.beacon_selected, -1.0f, {}, hk::kBeacon);
    if (tiles.empty()) return 0.0f;
    const float gap = 0.5f * kPad, row_h = 0.5f * (height - gap), col_w = text_height_units() * 6.5f;
    const size_t cols = (tiles.size() + 1) / 2;
    const float width = static_cast<float>(cols) * col_w + static_cast<float>(cols - 1) * gap;
    if (!draw) return width;
    for (size_t i = 0; i < tiles.size(); ++i) {
        Tile& t = tiles[i];
        t.c = grid2_cell(i, top_left, {col_w, row_h}, gap);
        t.size = {col_w, row_h};
        tile(t);
        ++stats_.actions;
        if (t.enabled) ++stats_.actions_live;
    }
    return width;
}

// #587 with nothing selected: the wings (as the selection header's), the fleet groups (selecting a group's members, as
// Classic's fleet tab) and the station (selecting it), as text buttons in two rows.
Vec2f ModernTacticalHud::grid2_cell(size_t i, Vec2f top_left, Vec2f cell, float gap) const {
    return {top_left.x + 0.5f * cell.x + static_cast<float>(i / 2) * (cell.x + gap), top_left.y - 0.5f * cell.y - static_cast<float>(i % 2) * (cell.y + gap)};
}

float ModernTacticalHud::build_shortcuts(const TacticalHudFrame& f, std::span<const HudUnit> units, std::span<const HudTypeInfo> types,
                                         Vec2f top_left, float height, bool draw) {
    struct Entry {
        int32_t id;
        std::u32string label;
        bool selected;
    };
    std::vector<Entry> entries;
    for (size_t w = 0; w < f.wings.size() && w < kMaxWings; ++w) {
        const HudWing& wing = f.wings[w];
        if (draw) wing_ids_.push_back(wing.squadrons > 0 ? wing.ids : std::vector<uint32_t>{});
        if (wing.squadrons == 0 || wing.ids.empty()) continue;
        char n[16];
        std::snprintf(n, sizeof n, " %u", wing.squadrons);
        entries.push_back({kWingHitBase + static_cast<int32_t>(w), wing.label + ascii32(n), wing.selected});
    }
    int32_t fleet_n = 0;  // the shortcut's index (fleet_shortcuts_)
    if (f.fleet_groups_on)
        for (size_t k = 0; k < f.fleet_groups.size(); ++k) {
            const HudFleetGroup& g = f.fleet_groups[k];
            if (g.members.empty()) continue;
            char n[24];
            std::snprintf(n, sizeof n, "%u", static_cast<unsigned>(g.id));
            std::u32string label = text_or_port(text_, nullptr, U"Group ") + ascii32(n);
            if (!g.name.empty()) label += U" " + g.name;
            if (draw) fleet_shortcuts_.push_back(g.members);
            entries.push_back({kModernFleetShortcutBase + fleet_n++, std::move(label), false});
        }
    if (f.station != commands::kNoObject) {  // named by its type (a space station, a land build pad)
        if (draw) station_ = f.station;
        std::u32string label;
        for (const HudUnit& u : units)
            if (u.id == f.station && u.type < types.size()) label = type_label(&types[u.type], text_);
        entries.push_back({kModernStationHit, label.empty() ? text_or_port(text_, nullptr, U"Station") : std::move(label), false});
    }
    if (entries.empty()) return 0.0f;
    HudTextStyle st;
    st.font = font();
    st.color = colours_.text;
    st.align = TextAlign::Center;
    const float gap = 0.5f * kPad, row_h = 0.5f * (height - gap);
    float col_w = 0.0f;
    for (const Entry& e : entries) col_w = std::max(col_w, measure_text_block(*fonts_, canvas_, e.label, 0.0f, st).width + 2.0f * kPad);
    const size_t cols = (entries.size() + 1) / 2;
    const float width = static_cast<float>(cols) * col_w + static_cast<float>(cols - 1) * gap;
    if (!draw) return width;
    for (size_t i = 0; i < entries.size(); ++i) {
        Tile t;
        t.id = entries[i].id;
        t.text = entries[i].label;
        t.selected = entries[i].selected;
        t.c = grid2_cell(i, top_left, {col_w, row_h}, gap);
        t.size = {col_w, row_h};
        tile(t);
        ++stats_.shortcuts;
    }
    return width;
}

// #587 production, while a builder with options is selected: the builder card, the options (Classic's Build_NN ids) with
// their cost or blocked reason, and the build queue (tqueueNN) with each entry's progress; returns the width (0: none).
float ModernTacticalHud::build_production(const TacticalHudFrame& f, std::span<const HudUnit> units, std::span<const HudTypeInfo> types,
                                          Vec2f top_left, float height, float max_width) {
    const float line = text_height_units(), gap = 0.5f * kPad, row_h = 0.5f * (height - gap);
    const float card_w = line * 8.0f, opt_w = line * 8.0f;
    float x = top_left.x;
    // The builder card: its name over "Production".
    {
        const HudUnit* b = nullptr;
        for (const HudUnit& u : units)
            if (u.id == f.builder) b = &u;
        const HudTypeInfo* t = b && b->type < types.size() ? &types[b->type] : nullptr;
        const Vec2f c{x + 0.5f * card_w, top_left.y - 0.5f * height};
        frame_rect(dl_, kControlLayer, c, {card_w, height}, colours_.button, accent(200));
        HudTextStyle st;
        st.font = font();
        st.color = colours_.text;
        st.align = TextAlign::Center;
        const std::u32string name = type_label(t, text_);
        st.width_scale = fit_width_scale(measure_text_block(*fonts_, canvas_, name, 0.0f, st).width, card_w - kPad);
        add_text(dl_, kControlLayer, HudList::Text, *fonts_, canvas_, name, {c.x, c.y + 0.6f * line}, st);
        HudTextStyle ls = st;
        ls.width_scale = 1.0f;
        ls.color = colours_.label;
        add_text(dl_, kControlLayer, HudList::Text, *fonts_, canvas_, text_or_port(text_, nullptr, U"Production"), {c.x, c.y - 0.6f * line}, ls);
        x += card_w + 2.0f * gap;
    }
    // The options: 3 columns of 2 rows, the icon over the cost (or the blocked reason, dashed). On land with the pad on
    // screen they sit on the pad itself instead (#592, build_pad_options).
    const bool on_pad = f.world.land && f.has_build_anchor;
    for (size_t i = 0; !on_pad && i < f.build_options.size() && i < buy_.size(); ++i) {
        if (buy_[i] < 0) continue;
        const Tile t = option_tile(i, f.build_options[i], grid2_cell(i, {x, top_left.y}, {opt_w, row_h}, gap), {opt_w, row_h});
        if (t.c.x + 0.5f * opt_w > top_left.x + max_width) break;
        add_option(t, i, f.build_options[i]);
    }
    if (on_pad) build_pad_options(f);
    else if (!f.build_options.empty()) x += 3.0f * opt_w + 2.0f * gap + 2.0f * gap;
    x += build_queue(f, {x, top_left.y}, height, top_left.x + max_width - x, true, true);
    stats_.production = true;
    return x - top_left.x;
}

ModernTacticalHud::Tile ModernTacticalHud::option_tile(size_t i, const HudBuildOption& o, Vec2f c, Vec2f size) const {
    Tile t;
    t.id = buy_[i];
    t.c = c;
    t.size = size;
    t.icon = o.icon;
    t.enabled = o.block == HudBuildOption::Block::None;
    t.text = build_option_text(o, text_);
    if (o.queued > 1) t.badge = count_text(o.queued);  // as Classic's text (shown above 1)
    return t;
}

void ModernTacticalHud::add_option(const Tile& t, size_t i, const HudBuildOption& o) {
    tile(t);
    stats_.build_texts.push_back(t.text);
    buy_slots_.push_back({buy_[i], o});
    ++stats_.build_options;
    if (!t.enabled) ++stats_.build_blocked;
}

// #592 the build pad's buy buttons on the pad: Classic's placement (TacticalHud::build_buy_buttons: the i_build_buttons
// bones round the pad's projected point, 52 x 52 each), Modern's look; the same options, costs, blocked reasons and clicks.
void ModernTacticalHud::build_pad_options(const TacticalHudFrame& f) {
    static constexpr Vec2f kBone[6] = {{-54.0f, -67.0f}, {-0.5f, -67.0f}, {52.0f, -67.0f}, {-54.0f, -120.0f}, {-0.5f, -120.0f}, {52.0f, -120.0f}};
    static constexpr Vec2f kHitOffset{1.0f, -4.0f};
    const Vec2f anchor = canvas_.pixel_to_canvas(f.build_anchor_px.x, f.build_anchor_px.y);
    for (size_t i = 0; i < f.build_options.size() && i < buy_.size() && i < std::size(kBone); ++i) {
        if (buy_[i] < 0) continue;
        const Vec2f c{anchor.x + kBone[i].x + kHitOffset.x, anchor.y + kBone[i].y + kHitOffset.y};
        add_option(option_tile(i, f.build_options[i], c, {52.0f, 52.0f}), i, f.build_options[i]);
    }
    stats_.pad_options = true;
}

// #587 the build queue (Classic's tqueueNN slots, in its order): square entries with a progress bar and the count; a click
// cancels (Classic's TacticalCancel). `builder_only`: only the selected builder's entries. Returns the width (0: none).
float ModernTacticalHud::build_queue(const TacticalHudFrame& f, Vec2f top_left, float height, float max_width, bool builder_only, bool draw) {
    if (!f.providers.build_queue) return 0.0f;
    std::vector<HudQueueEntry> entries;
    f.providers.build_queue(entries);
    const float gap = 0.5f * kPad, side = 0.5f * (height - gap);
    float width = 0.0f;
    size_t shown = 0;
    for (size_t i = 0; i < entries.size() && i < queue_.size(); ++i) {
        const HudQueueEntry& e = entries[i];
        if (queue_[i] < 0 || (builder_only && e.location != f.builder)) continue;
        const Vec2f c = grid2_cell(shown, top_left, {side, side}, gap);
        if (c.x + 0.5f * side > top_left.x + max_width) break;
        ++shown;
        width = c.x + 0.5f * side - top_left.x;
        if (!draw) continue;
        Tile t;
        t.id = queue_[i];  // the entry's own slot: Classic's id for the same entry
        t.c = c;
        t.size = {side, side};
        t.icon = e.icon;
        if (e.count > 1) t.badge = count_text(e.count);
        tile(t);
        add_bar(dl_, kControlLayer, {c.x, c.y - 0.5f * side + 4.0f}, {side - 6.0f, 4.0f}, std::clamp(e.progress, 0.0f, 1.0f), accent(255), false);
        queue_slots_.push_back({queue_[i], e});
        ++stats_.queue_entries;
    }
    return width;
}

// #587 the reinforcement list while open (Classic's r_RRCC entries): each entry's icon and count; unavailable ones dashed.
// A click starts the drop point pick (HudAction::BeginReinforcement, as Classic's).
float ModernTacticalHud::build_reinforcements(const TacticalHudFrame& f, Vec2f top_left, float height, float max_width) {
    std::vector<HudReinforcement> entries;
    f.providers.reinforcements(entries);
    const float gap = 0.5f * kPad, side = 0.5f * (height - gap);
    float width = 0.0f;
    for (size_t i = 0; i < entries.size() && i < reinforce_.size(); ++i) {
        if (reinforce_[i] < 0) continue;
        const Vec2f c = grid2_cell(i, top_left, {side, side}, gap);
        if (c.x + 0.5f * side > top_left.x + max_width) break;
        const HudReinforcement& e = entries[i];
        Tile t;
        t.id = reinforce_[i];
        t.c = c;
        t.size = {side, side};
        t.icon = e.icon;
        t.enabled = e.available;
        if (e.count > 1) t.badge = count_text(e.count);
        tile(t);
        reinforce_slots_.push_back({reinforce_[i], e});
        ++stats_.reinforcements;
        width = c.x + 0.5f * side - top_left.x;
    }
    return width;
}

// #586 a recharge ring round an ability button: the recharged part (from 12 o'clock, clockwise) in the accent, the rest dim.
void ModernTacticalHud::ability_ring(Vec2f c, float radius, float recharge) {
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

// #586 the abilities section (left of the orders): the selection's ability buttons (selection_ability_buttons on the
// Classic grouping, so the same buttons, users and order as Classic) in two rows; each with its icon, a recharge ring and
// the seconds left while recharging, the AUTO badge while autofire is on, the active look while on. Returns the width.
float ModernTacticalHud::build_abilities(const TacticalHudFrame& f, std::span<const HudUnit> units, Vec2f top_right, float height,
                                         float max_width) {
    if (!f.providers.abilities) return 0.0f;
    const std::vector<SelectedGroup> classic = f.fleet_groups_on ? group_selection_fleets(units, f.fleet_groups, panel_slots_)
                                                                 : group_selection(units);
    std::vector<HudAbilityButton> buttons = selection_ability_buttons(classic, f.providers, special_button_.size());
    if (buttons.empty()) return 0.0f;
    const float gap = 0.5f * kPad;
    const float side = 0.5f * (height - gap);
    const auto fit_cols = static_cast<size_t>(std::max(1.0f, std::floor((max_width + gap) / (side + gap))));
    const size_t cols = std::min((buttons.size() + 1) / 2, fit_cols);
    const float width = static_cast<float>(cols) * side + static_cast<float>(cols - 1) * gap;
    HudTextStyle st;
    st.font = font();
    st.color = colours_.text;
    st.align = TextAlign::Center;
    for (size_t i = 0; i < buttons.size(); ++i) {
        AbilityButton b;
        static_cast<HudAbilityButton&>(b) = std::move(buttons[i]);
        b.component = special_button_[i];
        // Past the deck's room (or without a component) the button is not drawn but its hotkey still fires, as Classic's.
        if (i >= 2 * cols || b.component < 0) {
            abilities_.push_back(std::move(b));
            continue;
        }
        const HudAbility& a = b.ability;
        const Vec2f c{top_right.x - width + 0.5f * side + static_cast<float>(i / 2) * (side + gap),
                      top_right.y - 0.5f * side - static_cast<float>(i % 2) * (side + gap)};
        const Vec2f size{side, side};
        const bool hovered = hover_ == b.component;
        if (!a.enabled) {
            add_rect(dl_, kControlLayer, c.x - 0.5f * side, c.y - 0.5f * side, c.x + 0.5f * side, c.y + 0.5f * side, Rgba8{16, 20, 28, 160});
            dashed_rect(dl_, kControlLayer, c, size, colours_.disabled);
        } else if (a.active) {  // the active look: the accent filled, a bright edge and a corner notch (a shape, not only colour)
            frame_rect(dl_, kControlLayer, c, size, accent(150), colours_.text);
            HudQuad notch;
            notch.has_corners = true;
            const float n = 0.3f * side, x0 = c.x - 0.5f * side, y1 = c.y + 0.5f * side;
            notch.corners = {Vec2f{x0, y1}, Vec2f{x0 + n, y1}, Vec2f{x0 + n, y1}, Vec2f{x0, y1 - n}};
            notch.color = colours_.text;
            dl_.add(kControlLayer, HudList::Alpha, notch);
            ++stats_.ability_active;
        } else {
            frame_rect(dl_, kControlLayer, c, size, colours_.button, hovered ? colours_.text : accent(200));
        }
        if (!a.icon.empty() && atlas_)
            add_atlas_quad(dl_, kControlLayer, HudList::Alpha, *atlas_, a.icon, c, {0.8f * side, 0.8f * side}, 1.0f,
                           a.enabled ? Rgba8{255, 255, 255, 255} : Rgba8{128, 128, 128, 255});
        if (a.recharge < 1.0f) {
            ability_ring(c, 0.5f * side - 2.0f, std::clamp(a.recharge, 0.0f, 1.0f));
            char n[16];
            if (a.seconds_left >= 0.0f) std::snprintf(n, sizeof n, "%d", static_cast<int>(std::ceil(a.seconds_left)));
            else std::snprintf(n, sizeof n, "%d%%", static_cast<int>(std::clamp(a.recharge, 0.0f, 1.0f) * 100.0f));
            const std::u32string count = ascii32(n);
            const float th = text_height_units();
            add_rect(dl_, kControlLayer, c.x - 0.4f * side, c.y - 0.6f * th, c.x + 0.4f * side, c.y + 0.6f * th, Rgba8{0, 0, 0, 170});
            add_text(dl_, kControlLayer, HudList::Text, *fonts_, canvas_, count, c, st);
            stats_.ability_counts.push_back(count);
            ++stats_.ability_rings;
        }
        if (a.autofire) {  // AUTO along the bottom edge
            HudTextStyle as = st;
            const std::u32string label = text_or_port(text_, nullptr, U"AUTO");
            const float lh = text_height_units() * 1.1f;
            const float lw = std::min(side, measure_text_block(*fonts_, canvas_, label, 0.0f, as).width + kPad);
            as.width_scale = fit_width_scale(measure_text_block(*fonts_, canvas_, label, 0.0f, as).width, lw - 2.0f);
            const Vec2f lc{c.x, c.y - 0.5f * side + 0.5f * lh};
            frame_rect(dl_, kControlLayer, lc, {lw, lh}, accent(230), colours_.text);
            as.color = Rgba8{0, 0, 0, 255};
            add_text(dl_, kControlLayer, HudList::Text, *fonts_, canvas_, label, lc, as);
            ++stats_.ability_auto;
        }
        if (keys_ && !a.type_name.empty())
            if (const int32_t slot = ability_hotkey_slot(*keys_, a.type_name); slot >= 0)
                key_badge(static_cast<uint16_t>(slot), {c.x + 0.5f * side - 2.0f, c.y + 0.5f * side - 2.0f}, kControlLayer);
        hit(b.component, kControlLayer, c, size);
        b.drawn = true;
        b.center = c;
        b.size = size;
        abilities_.push_back(std::move(b));
        ++stats_.abilities;
    }
    return width;
}

bool ModernTacticalHud::activate_ability(uint8_t ability_type, bool queued, std::vector<commands::EventData>& out,
                                         std::vector<HudAction>* actions) const {
    if (!table_ || ability_type == 0) return false;
    for (const AbilityButton& a : abilities_) {
        if (a.ability.ability_type != ability_type) continue;
        if (!a.ability.enabled || a.users.empty()) return false;
        ability_button_click(a, a.component, a.component >= 0 ? (*table_)[static_cast<size_t>(a.component)].name : std::string(),
                             Button::Left, queued, out, actions);
        return true;
    }
    return false;
}

bool ModernTacticalHud::ability_box(uint8_t ability_type, Vec2f& center, Vec2f& size) const {
    for (const AbilityButton& a : abilities_)
        if (a.ability.ability_type == ability_type && a.drawn) {
            center = a.center;
            size = a.size;
            return true;
        }
    return false;
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
    // Cards: the Classic grouping; with more groups than fit, the last card is "+N" (every unit not shown). With fleet groups
    // on, a row of group tabs over the cards (#588).
    const float tab_h = f.fleet_groups_on ? line * 1.4f : 0.0f;
    const float card_h = height - header - 0.5f * kPad - tab_h, card_w = std::max(card_h * 0.8f, 6.0f * line);
    const float gap = 0.5f * kPad;
    const auto fit = static_cast<size_t>(std::max(1.0f, std::floor((width + gap) / (card_w + gap))));
    if (f.fleet_groups_on) {
        groups_ = group_selection_fleets(units, f.fleet_groups, fit);
    } else {
        groups_ = group_selection(units);  // the Classic grouping and its cap
        fold_selection_overflow(groups_, fit);
    }
    const float cy = top_left.y - header - 0.5f * kPad - tab_h - 0.5f * card_h;
    if (f.fleet_groups_on) build_fleet_tabs(f, units, {{top_left.x + 0.5f * card_w, cy}, {card_w, card_h}, gap, top_left.x + width}, tab_h);
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

// #588 the fleet group tabs (Classic's build_fleet_tabs rules: fleet_tab_state): each run of a group's cards framed, its
// tab over the run with the group's number and name (a padlock and the warning colour while locked) and its Merge / Split
// buttons; the tab's members are the feed group's (or the run's units).
void ModernTacticalHud::build_fleet_tabs(const TacticalHudFrame& f, std::span<const HudUnit> units, const CardRow& row, float tab_h) {
    const float card_w = row.card.x, card_h = row.card.y, gap = row.gap;
    const Vec2f first_card = row.first;
    const std::vector<size_t> selected = fleet_selected_counts(units, f.fleet_groups);
    HudTextStyle st;
    st.font = font();
    st.color = colours_.text;
    st.align = TextAlign::Left;
    HudTextStyle bs = st;
    bs.align = TextAlign::Center;
    for (size_t first = 0; first < groups_.size();) {
        const uint16_t id = groups_[first].fleet;
        size_t end = first + 1;  // the run [first, end): one group's cards
        while (end < groups_.size() && groups_[end].fleet == id && !groups_[end].overflow) ++end;
        if (id == 0 || groups_[first].overflow) {
            first = end;
            continue;
        }
        const size_t s = first, e = end;
        const HudFleetTabState ts = fleet_tab_state(f.fleet_groups, selected, id);
        const bool locked = ts.group && ts.group->locked;
        const Rgba8 edge = locked ? colours_.hull_warn : accent(230);
        const float x0 = first_card.x - 0.5f * card_w + static_cast<float>(s) * (card_w + gap);
        const float x1 = first_card.x - 0.5f * card_w + static_cast<float>(e - 1) * (card_w + gap) + card_w;
        // The run's frame (round the cards) and its tab.
        frame_rect(dl_, kControlLayer, {0.5f * (x0 + x1), first_card.y}, {x1 - x0 + 4.0f, card_h + 4.0f}, Rgba8{0, 0, 0, 0}, edge);
        char num[16];
        std::snprintf(num, sizeof num, "%u", static_cast<unsigned>(id));
        std::u32string label = ascii32(num);
        if (ts.group && !ts.group->name.empty()) label += U" " + ts.group->name;
        const std::u32string merge = text_or_port(text_, nullptr, U"Merge"), split = text_or_port(text_, nullptr, U"Split");
        const float mw = measure_text_block(*fonts_, canvas_, merge, 0.0f, bs).width + kPad;
        const float sw = measure_text_block(*fonts_, canvas_, split, 0.0f, bs).width + kPad;
        const float lock_w = locked ? tab_h : 0.0f;
        const float buttons = (ts.merge ? mw + 2.0f : 0.0f) + (ts.split ? sw + 2.0f : 0.0f);
        // As wide as the run, or its label and buttons, up to the next run's start (or the section's end): the next tab never
        // covers this one's buttons; the label is squeezed to the room left.
        const float room = (e < groups_.size() ? first_card.x - 0.5f * card_w + static_cast<float>(e) * (card_w + gap) - 0.5f * gap : row.right) - (x0 - 2.0f);
        const float label_w = measure_text_block(*fonts_, canvas_, label, 0.0f, st).width;
        const float tab_w = std::max(x1 - x0 + 4.0f, std::min(room, lock_w + label_w + buttons + 2.0f * kPad));
        st.width_scale = fit_width_scale(label_w, std::max(1.0f, tab_w - lock_w - buttons - 2.0f * kPad));
        const Vec2f tab{x0 - 2.0f + 0.5f * tab_w, first_card.y + 0.5f * card_h + 2.0f + 0.5f * tab_h};
        const int32_t index = static_cast<int32_t>(fleet_tab_ids_.size());
        frame_rect(dl_, kControlLayer, tab, {tab_w, tab_h}, colours_.button, hover_ == kFleetTabHitBase + index ? colours_.text : edge);
        float tx = tab.x - 0.5f * tab_w + kPad;
        if (locked) {  // a padlock: shackle over a body (a shape as well as the colour)
            const float u = 0.25f * tab_h;
            frame_rect(dl_, kControlLayer, {tx + u, tab.y + 0.5f * u}, {u, u}, Rgba8{0, 0, 0, 0}, edge);
            add_rect(dl_, kControlLayer, tx + 0.4f * u, tab.y - 0.9f * u, tx + 1.6f * u, tab.y + 0.1f * u, edge);
            tx += lock_w;
        }
        add_text(dl_, kControlLayer, HudList::Text, *fonts_, canvas_, label, {tx, tab.y}, st);
        hit(kFleetTabHitBase + index, kControlLayer, tab, {tab_w, tab_h});
        float bx = tab.x + 0.5f * tab_w - 1.0f;
        auto button = [&](int32_t base, const std::u32string& text, float w) {  // a layer over the tabs, as Classic's
            const Vec2f bc{bx - 0.5f * w, tab.y};
            bx -= w + 2.0f;
            frame_rect(dl_, kControlLayer + 1, bc, {w, tab_h - 4.0f}, accent(120), hover_ == base + index ? colours_.text : edge);
            add_text(dl_, kControlLayer + 1, HudList::Text, *fonts_, canvas_, text, bc, bs);
            hit(base + index, kControlLayer + 1, bc, {w, tab_h - 4.0f});
        };
        if (ts.split) {
            button(kFleetSplitHitBase, split, sw);
            ++stats_.fleet_split;
        }
        if (ts.merge) {
            button(kFleetMergeHitBase, merge, mw);
            ++stats_.fleet_merge;
        }
        std::vector<uint32_t> members;
        if (ts.group) members = ts.group->members;
        else
            for (size_t k = s; k < e; ++k) members.insert(members.end(), groups_[k].ids.begin(), groups_[k].ids.end());
        fleet_tab_ids_.push_back(std::move(members));
        fleet_tab_group_.emplace_back(id, ts.group ? utf32_to_utf8(ts.group->name) : std::string{});
        ++stats_.fleet_tabs;
        st.width_scale = 1.0f;
        first = end;
    }
}

namespace {
// #591 map blip shapes (a shape as well as the team colour): square, diamond (a turned square), circle (an octagon).
void blip_square(HudDrawList& dl, uint32_t layer, Vec2f c, float r, Rgba8 col) { add_rect(dl, layer, c.x - r, c.y - r, c.x + r, c.y + r, col); }
void blip_diamond(HudDrawList& dl, uint32_t layer, Vec2f c, float r, Rgba8 col) {
    HudQuad q;
    q.has_corners = true;
    q.corners = {Vec2f{c.x, c.y + 1.3f * r}, Vec2f{c.x + 1.3f * r, c.y}, Vec2f{c.x, c.y - 1.3f * r}, Vec2f{c.x - 1.3f * r, c.y}};
    q.color = col;
    dl.add(layer, HudList::Alpha, q);
}
void blip_circle(HudDrawList& dl, uint32_t layer, Vec2f c, float r, Rgba8 col) {
    const float k = 0.4142f * r;  // the octagon's half side
    add_rect(dl, layer, c.x - r, c.y - k, c.x + r, c.y + k, col);
    for (const float s : {1.0f, -1.0f}) {  // the top and bottom trapezoids
        HudQuad q;
        q.has_corners = true;
        q.corners = {Vec2f{c.x - k, c.y + s * r}, Vec2f{c.x + k, c.y + s * r}, Vec2f{c.x + r, c.y + s * k}, Vec2f{c.x - r, c.y + s * k}};
        q.color = col;
        dl.add(layer, HudList::Alpha, q);
    }
}
void line_quad(HudDrawList& dl, uint32_t layer, Vec2f a, Vec2f b, float w, Rgba8 col) {
    const float dx = b.x - a.x, dy = b.y - a.y, len = std::sqrt(dx * dx + dy * dy);
    if (len < 1e-3f) return;
    const Vec2f n{-dy / len * 0.5f * w, dx / len * 0.5f * w};
    HudQuad q;
    q.has_corners = true;
    q.corners = {Vec2f{a.x + n.x, a.y + n.y}, Vec2f{b.x + n.x, b.y + n.y}, Vec2f{b.x - n.x, b.y - n.y}, Vec2f{a.x - n.x, a.y - n.y}};
    q.color = col;
    dl.add(layer, HudList::Alpha, q);
}
}  // namespace

// #591 the Modern map: the Classic radar's feed and rules (radar_blip_shown, RadarView, the radar image page, the events,
// the camera footprint) in a square panel above the deck's left end (right with the map side option).
void ModernTacticalHud::build_map(const TacticalHudFrame& f, std::span<const HudUnit> units, std::span<const HudTypeInfo> types) {
    const float side = map_side();
    const float bottom = -0.5f * canvas_.height + kMargin;  // level with the deck's bottom edge, at its end
    const float cx = options_.map_right ? 0.5f * canvas_.width - kMargin - 0.5f * side : -0.5f * canvas_.width + kMargin + 0.5f * side;
    const Vec2f center{cx, bottom + 0.5f * side}, size{side - 2.0f * kPad, side - 2.0f * kPad};
    frame_rect(dl_, kPanelLayer, center, {side, side}, colours_.back, accent(200));
    // The backdrop (RadarMap.xml's land / space grid, as Classic's), then terrain and fog.
    const std::string backdrop = f.radar_settings ? (f.world.land ? f.radar_settings->land_backdrop : f.radar_settings->space_backdrop)
                                                  : std::string("i_radar_map_grid.tga");
    if (atlas_) add_atlas_quad(dl_, kPanelLayer, HudList::Alpha, *atlas_, backdrop, center, size, 1.0f, Rgba8{255, 255, 255, 160});
    if (f.radar_image && !f.radar_image->empty()) {  // terrain and fog: the host's radar page
        HudQuad q;
        q.min = {center.x - 0.5f * size.x, center.y - 0.5f * size.y};
        q.max = {center.x + 0.5f * size.x, center.y + 0.5f * size.y};
        q.page = kHudPageRadar;
        dl_.add(kPanelLayer, HudList::Alpha, q);
    }
    hit(map_id(), kControlLayer, center, size);
    stats_.map = true;
    stats_.map_right = options_.map_right;
    map_view_.center = center;
    map_view_.size = size;
    map_view_.min_x = f.map_min_x;
    map_view_.min_y = f.map_min_y;
    map_view_.max_x = f.map_max_x;
    map_view_.max_y = f.map_max_y;
    if (!map_view_.valid()) return;
    const int32_t local = f.world.local_player;
    const float r = std::max(2.0f, 0.012f * side);
    for (const HudUnit& u : units) {
        const HudTypeInfo* t = u.type < types.size() ? &types[u.type] : nullptr;
        const bool hostile = hud_hostile(f.world, u.owner);
        if (!radar_blip_shown(u, t, hostile)) continue;
        const Vec2f c = map_view_.to_canvas(u.position.x, u.position.y);
        if (!map_view_.inside(c)) continue;
        Rgba8 col = u.selected && f.radar_colorize_selected ? f.radar_selected_color : u.team_color;
        col.a = 255;
        // A type's Radar_Icon_Name (mods' too) sits inside its shape: the shape grows to frame it, the icon in the blip
        // colour over a dark centre; Radar_Register_As_Foreground_Object a layer up.
        const bool icon = t && !t->radar_icon.empty() && atlas_ && atlas_->find(t->radar_icon);
        const float ur = (u.selected ? 1.4f : 1.0f) * (icon ? 2.2f * r : r);
        const uint32_t layer = kControlLayer + (t && t->radar_foreground ? 2u : 1u);
        if (u.owner == local && local >= 0) {
            blip_square(dl_, layer, c, ur, col);
            ++stats_.map_squares;
        } else if (hostile) {
            blip_diamond(dl_, layer, c, ur, col);
            ++stats_.map_diamonds;
        } else {
            blip_circle(dl_, layer, c, ur, col);
            ++stats_.map_circles;
        }
        if (icon) {
            add_rect(dl_, layer, c.x - 0.6f * ur, c.y - 0.6f * ur, c.x + 0.6f * ur, c.y + 0.6f * ur, Rgba8{0, 0, 0, 200});
            const float th = t->radar_show_facing && u.yaw_deg != 0.0f ? u.yaw_deg * 0.017453292519943295f - 1.5707963267948966f : 0.0f;
            add_atlas_quad_turned(dl_, layer, HudList::Alpha, *atlas_, t->radar_icon, c, {1.1f * ur, 1.1f * ur}, 1.0f, col, th, t->radar_rotate);
        }
    }
    // #592 land reinforcement zones: an enabling radius as a solid ring, a prevention radius as a dashed one in the warning
    // colour (a pattern as well as a colour); only the parts inside the map.
    for (const HudDropZone& z : f.drop_zones) {
        const Vec2f c = map_view_.to_canvas(z.x, z.y);
        const float rr = z.radius / (map_view_.max_x - map_view_.min_x) * map_view_.size.x;
        if (rr <= 0.5f) continue;
        constexpr int kSeg = 32;
        for (int d = 0; d < kSeg; ++d) {
            if (!z.enabling && d % 2 == 1) continue;
            const float a0 = 6.2831853f * static_cast<float>(d) / kSeg, a1 = 6.2831853f * static_cast<float>(d + 1) / kSeg;
            const Vec2f p0{c.x + rr * std::cos(a0), c.y + rr * std::sin(a0)}, p1{c.x + rr * std::cos(a1), c.y + rr * std::sin(a1)};
            if (map_view_.inside(p0) && map_view_.inside(p1))
                line_quad(dl_, kControlLayer + 1, p0, p1, 1.5f, z.enabling ? colours_.hull_ok : colours_.hull_bad);
        }
        ++(z.enabling ? stats_.map_drop_zones : stats_.map_block_zones);
    }
    if (f.radar_events)  // pings, as Classic's: a model for the host, else an expanding, fading ring; story blips pulse
        for (const RadarEvent& e : f.radar_events->items()) {
            const Vec2f c = map_view_.to_canvas(e.x, e.y);
            if (!map_view_.inside(c)) continue;
            ++stats_.map_pings;
            const RadarEventType* et = !e.marker && f.radar_settings ? f.radar_settings->event(e.type) : nullptr;
            if (et && !et->model.empty()) {
                radar_models_.push_back({et->model, c, et->scale * 0.5f * canvas_.width, e.age, e.id});
                if (f.radar_event_models) continue;
            }
            float tt = e.persistent ? std::fmod(e.age, 1.0f) : std::clamp(e.age / std::max(e.duration, 1e-3f), 0.0f, 1.0f);
            float rr = r * (2.0f + 6.0f * tt);
            auto a = static_cast<uint8_t>(std::lround(255.0f * (1.0f - 0.8f * tt)));
            if (e.marker) {  // #320: alpha = the pulse, scale = pulse / 512 + 0.5
                const float v = std::clamp(e.pulse, 0.0f, 255.0f);
                rr = 4.0f * r * (v / 512.0f + 0.5f);
                a = static_cast<uint8_t>(v);
            }
            for (int d = 0; d < 16; ++d) {
                const float a0 = 6.2831853f * static_cast<float>(d) / 16.0f, a1 = 6.2831853f * static_cast<float>(d + 1) / 16.0f;
                const Vec2f p0{c.x + rr * std::cos(a0), c.y + rr * std::sin(a0)}, p1{c.x + rr * std::cos(a1), c.y + rr * std::sin(a1)};
                if (map_view_.inside(p0) && map_view_.inside(p1)) line_quad(dl_, kControlLayer + 1, p0, p1, 1.5f, Rgba8{255, 255, 255, a});
            }
        }
    if (f.radar_footprint) {  // the camera's ground footprint as dotted lines, the dots outside the map dropped (as Classic's)
        const std::array<Vec2f, 4> fp = camera_footprint(f.world.view, f.ground_z);
        for (size_t i = 0; i < 4; ++i) {
            const Vec2f a = map_view_.to_canvas(fp[i].x, fp[i].y), b = map_view_.to_canvas(fp[(i + 1) % 4].x, fp[(i + 1) % 4].y);
            const float len = std::sqrt((b.x - a.x) * (b.x - a.x) + (b.y - a.y) * (b.y - a.y));
            const int n = std::min(400, static_cast<int>(len / 3.0f) + 1);
            for (int k = 0; k <= n; ++k) {
                const float tk = static_cast<float>(k) / static_cast<float>(n);
                const Vec2f q{a.x + (b.x - a.x) * tk, a.y + (b.y - a.y) * tk};
                if (map_view_.inside(q)) add_rect(dl_, kControlLayer + 1, q.x - 0.75f, q.y - 0.75f, q.x + 0.75f, q.y + 0.75f, Rgba8{255, 255, 255, 200});
            }
        }
    }
}

// #591 the hover target card: the hovered hostile unit (HudUnit::hovered) - its name, hull and shield bars with their
// percentages, and its hard points (the frame's, parent = the unit) as a bar each - beside it on screen. No hit box.
void ModernTacticalHud::build_target_card(const TacticalHudFrame& f, std::span<const HudUnit> units, std::span<const HudTypeInfo> types) {
    const HudUnit* u = nullptr;
    for (const HudUnit& x : units)
        if (x.hovered && hud_hostile(f.world, x.owner) && !x.stealthed) u = &x;
    if (!u) return;
    float sx = 0.0f, sy = 0.0f;
    if (!project(f.world.view, u->position, sx, sy)) return;
    const Vec2f at = canvas_.pixel_to_canvas(sx, sy);
    std::vector<const HudHardPoint*> points;
    for (const HudHardPoint& hp : f.hardpoints)
        if (hp.parent == u->id) points.push_back(&hp);
    const float line = text_height_units() * 1.3f, w = 14.0f * text_height_units();
    const size_t hp_rows = std::min<size_t>(points.size(), 8);
    const float h = line * (2.0f + (u->shield >= 0.0f ? 1.0f : 0.0f) + (hp_rows > 0 ? 1.0f + static_cast<float>(hp_rows) * 0.6f : 0.0f)) + kPad;
    Vec2f c{at.x + 40.0f + 0.5f * w, at.y};
    c.x = std::clamp(c.x, -0.5f * canvas_.width + kMargin + 0.5f * w, 0.5f * canvas_.width - kMargin - 0.5f * w);
    c.y = std::clamp(c.y, -0.5f * canvas_.height + kMargin + kDeckHeight + 0.5f * h, 0.5f * canvas_.height - kMargin - 0.5f * h);
    frame_rect(dl_, kBannerLayer, c, {w, h}, colours_.back, colours_.hull_bad);
    HudTextStyle st;
    st.font = font();
    st.color = colours_.text;
    const float left = c.x - 0.5f * w + kPad, bar_w = w - 2.0f * kPad;
    float y = c.y + 0.5f * h - 0.5f * kPad - 0.5f * line;
    const std::u32string name = type_label(u->type < types.size() ? &types[u->type] : nullptr, text_);
    st.width_scale = fit_width_scale(measure_text_block(*fonts_, canvas_, name, 0.0f, st).width, bar_w);
    add_text(dl_, kBannerLayer, HudList::Text, *fonts_, canvas_, name, {left, y}, st);
    st.width_scale = 1.0f;
    auto row = [&](const char32_t* label, float v, Rgba8 col, bool striped) {
        y -= line;
        char n[16];
        std::snprintf(n, sizeof n, " %d%%", static_cast<int>(std::lround(std::clamp(v, 0.0f, 1.0f) * 100.0f)));
        HudTextStyle ls = st;
        ls.color = colours_.label;
        add_text(dl_, kBannerLayer, HudList::Text, *fonts_, canvas_, text_or_port(text_, nullptr, label) + ascii32(n), {left, y + 0.2f * line}, ls);
        add_bar(dl_, kBannerLayer, {c.x, y - 0.35f * line}, {bar_w, 4.0f}, v, col, striped);
    };
    row(U"Hull", u->health, hull_colour(u->health), false);
    if (u->shield >= 0.0f) row(U"Shields", u->shield, colours_.shield, true);
    if (hp_rows > 0) {
        y -= line;
        HudTextStyle ls = st;
        ls.color = colours_.label;
        char n[24];
        std::snprintf(n, sizeof n, "%zu ", points.size());
        add_text(dl_, kBannerLayer, HudList::Text, *fonts_, canvas_, ascii32(n) + text_or_port(text_, nullptr, U"hard points"), {left, y}, ls);
        for (size_t i = 0; i < hp_rows; ++i) {
            y -= 0.6f * line;
            const HudHardPoint& hp = *points[i];
            add_bar(dl_, kBannerLayer, {c.x, y}, {bar_w, 4.0f}, hp.health, hp.disabled ? colours_.disabled : hull_colour(hp.health), hp.disabled);
        }
    }
    stats_.target_card = true;
    stats_.target_id = u->id;
    stats_.target_hardpoints = static_cast<uint32_t>(points.size());
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
    // #591 the map: Classic's radar input (TacticalHud::update_input "Radar"): a left press / drag jumps the camera to the
    // cursor's point; a right click with a selection moves it there (and pings).
    for (const HudPointerEvent& e : routed.hud) {
        if (e.button != Button::Left) continue;
        if (e.kind == HudPointerEvent::Kind::Press) map_drag_ = e.component == map_id() && map_view_.valid();
        if (e.kind == HudPointerEvent::Kind::Release) map_drag_ = false;
    }
    if (map_drag_ && routed.owner[static_cast<size_t>(Button::Left)] == PointerTarget::Hud && map_view_.valid()) {
        const Vec2f cp = canvas_.pixel_to_canvas(static_cast<float>(routed.world.mouse_x) + 0.5f, static_cast<float>(routed.world.mouse_y) + 0.5f);
        act(radar_jump_action(map_id(), map_view_.to_world(cp)));
    } else if (routed.owner[static_cast<size_t>(Button::Left)] != PointerTarget::Hud) {
        map_drag_ = false;
    }
    for (const HudPointerEvent& e : routed.hud) {
        if (e.kind != HudPointerEvent::Kind::Click || e.component < 0 || !drawn(e.component)) continue;
        if (e.component == map_id()) {
            if (e.button == Button::Right && select_groups_ && map_view_.valid())
                radar_move_click(map_view_.to_world(canvas_.pixel_to_canvas(static_cast<float>(e.x) + 0.5f, static_cast<float>(e.y) + 0.5f)), ground_z_,
                                 out, actions);
            continue;
        }
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
        // #587 shortcuts (Modern's own): a fleet group's members, the station.
        if (e.component >= kModernFleetShortcutBase && e.component - kModernFleetShortcutBase < static_cast<int32_t>(fleet_shortcuts_.size())) {
            const std::vector<uint32_t>& m = fleet_shortcuts_[static_cast<size_t>(e.component - kModernFleetShortcutBase)];
            if (e.button == Button::Left)
                out.push_back(commands::ev::select(std::vector<uint32_t>(m.begin(), m.begin() + static_cast<std::ptrdiff_t>(std::min<size_t>(m.size(), 127)))));
            continue;
        }
        if (e.component == kModernStationHit) {
            if (e.button == Button::Left && station_ != commands::kNoObject) out.push_back(commands::ev::select({station_}));
            continue;
        }
        if (e.component >= kModernHitBase) continue;  // a panel's back
        // #588 fleet group tabs, Merge and Split: the Classic ids and clicks (TacticalHud::update_input "fleet group header
        // tabs"): left selects the group (Shift adds it), right asks the host to rename it; Merge / Split act on the synced
        // selection.
        const auto tab_of = [&](int32_t base) {
            return e.component >= base && e.component - base < static_cast<int32_t>(fleet_tab_ids_.size()) ? static_cast<size_t>(e.component - base)
                                                                                                          : SIZE_MAX;
        };
        if (const size_t t = tab_of(kFleetTabHitBase); t != SIZE_MAX) {
            if (e.button == Button::Right) {
                act(fleet_rename_action(e.component, fleet_tab_group_[t].first, fleet_tab_group_[t].second));
            } else if (e.button == Button::Left) {
                out.push_back(commands::ev::select(fleet_tab_select_ids(fleet_tab_ids_[t], selection_ids(), shift)));
            }
            continue;
        }
        if (tab_of(kFleetMergeHitBase) != SIZE_MAX || tab_of(kFleetSplitHitBase) != SIZE_MAX) {
            if (e.button == Button::Left) out.push_back(fleet_button_event(tab_of(kFleetMergeHitBase) != SIZE_MAX));
            continue;
        }
        // Wing buttons: the Classic ids and action (TacticalHud::update_input "squadron wing buttons").
        if (e.component >= kWingHitBase && e.component < kFleetTabHitBase) {
            const auto w = static_cast<size_t>(e.component - kWingHitBase);
            if (e.button == Button::Left && w < wing_ids_.size() && !wing_ids_[w].empty())
                act(wing_select_action(static_cast<int32_t>(w), wing_ids_[w], shift, ctrl));
            continue;
        }
        // Ability buttons: the Classic click (TacticalHud::update_input "Ability buttons"; Shift = queued).
        if (const auto it = std::find_if(abilities_.begin(), abilities_.end(),
                                         [&](const AbilityButton& a) { return a.drawn && a.component == e.component; });
            it != abilities_.end()) {
            if (it->ability.enabled)
                ability_button_click(*it, it->component, (*table_)[static_cast<size_t>(e.component)].name, e.button, shift, out, actions);
            continue;
        }
        // #587 the build queue, buy buttons and reinforcement entries: Classic's actions (TacticalHud::update_input).
        if (const auto q = std::find_if(queue_slots_.begin(), queue_slots_.end(), [&](const auto& s) { return s.first == e.component; });
            q != queue_slots_.end()) {
            if (e.button == Button::Left) act(tactical_cancel_action(e.component, (*table_)[static_cast<size_t>(e.component)].name, q->second));
            continue;
        }
        if (const auto b = std::find_if(buy_slots_.begin(), buy_slots_.end(), [&](const auto& s) { return s.first == e.component; });
            b != buy_slots_.end()) {
            if (e.button == Button::Left && b->second.block == HudBuildOption::Block::None)
                act(tactical_buy_action(e.component, (*table_)[static_cast<size_t>(e.component)].name, builder_, b->second, shift, ctrl));
            continue;
        }
        if (const auto r = std::find_if(reinforce_slots_.begin(), reinforce_slots_.end(), [&](const auto& s) { return s.first == e.component; });
            r != reinforce_slots_.end()) {
            if (e.button == Button::Left && r->second.available)
                act(begin_reinforcement_action(e.component, (*table_)[static_cast<size_t>(e.component)].name, r->second));
            continue;
        }
        // A disabled battle action takes no click (the Classic HudButtons swallows it); Retreat sends the retreat command.
        if (std::find(dead_actions_.begin(), dead_actions_.end(), e.component) != dead_actions_.end()) continue;
        if (e.component == retreat_ && retreat_ >= 0) {
            if (e.button == Button::Left) out.push_back(commands::ev::retreat(true));
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
