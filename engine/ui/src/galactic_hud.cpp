#include "alamo/ui/galactic_hud.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>

#include "alamo/core/ascii.hpp"
#include "alamo/data/values.hpp"
#include "alamo/ui/gui_dialog.hpp"
#include "alamo/ui/hud_font.hpp"
#include "alamo/ui/hud_radar.hpp"
#include "ui_strings.hpp"

namespace alamo::ui {

namespace {

// Credits symbol + space before HUD credit amounts: '$' is drawn as the credits sign by the EmpireAtWar fonts.
constexpr const char32_t* kCreditsPrefix = U"$ ";

// [port: Gabriel, original screenshot] credit amounts with a thousands separator ("13,000").
std::u32string grouped32(int64_t v) {
    const std::u32string d = num32(v < 0 ? -v : v);
    std::u32string o;
    if (v < 0) o.push_back(U'-');
    for (size_t i = 0; i < d.size(); ++i) {
        if (i > 0 && (d.size() - i) % 3 == 0) o.push_back(U',');
        o.push_back(d[i]);
    }
    return o;
}

Rgba8 with_alpha(Rgba8 c, uint8_t a) {
    c.a = static_cast<uint8_t>((static_cast<uint32_t>(c.a) * a + 127) / 255);
    return c;
}

const std::string* pick(const std::vector<std::string>& v, int32_t i) {
    if (v.empty()) return nullptr;
    const size_t k = i < 0 ? 0 : std::min(static_cast<size_t>(i), v.size() - 1);
    return &v[k];
}

constexpr Rgba8 kGalacticRadarBackdrop{6, 10, 22, 255};  // [port] radar ground under the faceplate (build_radar)
// §5.2 queue00..09 bone x (i_galactic_controls.alo, canvas units from the left edge).
constexpr float kQueueDomeX[10] = {343.0f, 410.0f, 475.0f, 539.9f, 607.9f, 699.0f, 765.0f, 832.0f, 898.0f, 960.0f};
// §1.2 top-anchored attack-mode shells: model y 92 sits on the screen's top edge [port: 28.jpg].
constexpr float kAttackShellTop = 92.0f;
// §5.1 build pad cost text [port: green sampled by eye from 25.jpg; unaffordable red is L].
constexpr Rgba8 kCostAffordable{96, 236, 96, 255};
constexpr Rgba8 kCostUnaffordable{236, 72, 64, 255};
// GALAXY_MAP_PRESENTATION §1.4: planet fleet badges and the Ships_Per_Stack divisor (GameConstants, 5 in FoC [M]).
constexpr const char* kStealthBadge = "i_icon_stealth.tga";
constexpr const char* kRaidBadge = "i_icon_raid.tga";
constexpr uint32_t kShipsPerStack = 5;

}  // namespace

float planet_label_alpha(int32_t zoom_state, float z) {
    switch (zoom_state) {
        case 1: return 255.0f * std::clamp(1.0f - 10.0f * z, 0.0f, 1.0f);
        case 2: return 255.0f * std::clamp(10.0f * (z - 0.9f), 0.0f, 1.0f);  // [port] clamped at 255
        case 3: return 0.0f;
        default: return 255.0f;  // [port] no zoom state
    }
}

void apply_galactic_fallback_layout(const ComponentTable& table, const HudCanvas& cv, HudLayout& layout) {
    const float W = 0.5f * cv.width, H = 0.5f * cv.height;
    auto put = [&](std::string_view name, float x, float y) {
        const int32_t i = table.find(name);
        if (i >= 0) layout.set_fallback(static_cast<uint32_t>(i), {x, y});
    };
    put("Text_Credits", W - 70.0f, H - 14.0f);
    put("Text_Day_Credits", W - 70.0f, H - 30.0f);  // [port]
    put("Text_Pop_Cap", W - 190.0f, H - 14.0f);
    put("Text_Tech_Level", W - 280.0f, H - 14.0f);
    put("Text_Day_Counter", -W + 150.0f, H - 14.0f);
    put("b_option_g", -W + 18.0f, H - 18.0f);
    put("b_camera_g", -W + 50.0f, H - 18.0f);
    put("b_play_pause", -W + 82.0f, H - 18.0f);
    put("b_fast_forward", -W + 112.0f, H - 18.0f);
    put("radar_galactic", -W + 92.0f, -H + 92.0f);
    put("Text_Planet", -60.0f, -H + 160.0f);
    put("Text_Planetary_Income", -60.0f, -H + 144.0f);
    put("Movie_galactic", -W + 51.0f, -H + 288.0f);  // i_galactic_controls.alo bone (FIXPASS_GALACTIC §4.2)
    // GALACTIC_DIALOGS §5.4: planet_info has no fallback: it shows only on its organiser / map_shell bone.
    for (int k = 0; k < 26; ++k) {  // GALACTIC_DIALOGS §5.1: 13 columns, 54-unit pitch from x 307; even = row 1, odd = row 2
        char n[32];
        std::snprintf(n, sizeof n, "b_Create_%02d", k);
        put(n, -W + 307.0f + 54.0f * static_cast<float>(k / 2), -H + (k % 2 == 0 ? 92.0f : 37.0f));
    }
    // the build tabs' bones of i_galactic_controls.alo, in the same frame as b_Create_00 (bone 332 -> 307).
    put("b_space_tab", -W + 257.0f, -H + 92.6f);
    put("b_planet_tab", -W + 258.0f, -H + 34.7f);
    for (int k = 0; k < 10; ++k) {
        char n[32];
        std::snprintf(n, sizeof n, "queue%02d", k);
        put(n, -W + kQueueDomeX[k], -H + 174.0f);  // §5.2: the queue domes on the bar's top edge (bones queue00..09)
    }
    for (int f = 0; f < 3; ++f) {
        char n[32];
        std::snprintf(n, sizeof n, "q%d_fleet", f + 1);
        put(n, -W + 200.0f, 150.0f - 100.0f * static_cast<float>(f));
        for (int r = 0; r < 5; ++r)
            for (int c = 0; c < 7; ++c) {
                std::snprintf(n, sizeof n, "q%d_%02d%02d", f + 1, r, c);
                put(n, -W + 250.0f + 44.0f * static_cast<float>(c), 170.0f - 100.0f * static_cast<float>(f) - 44.0f * static_cast<float>(r));
            }
    }
    for (int k = 0; k < 20; ++k) {
        char n[32];
        std::snprintf(n, sizeof n, "land_fleet_%02d", k);
        put(n, -W + 250.0f + 44.0f * static_cast<float>(k % 10), -170.0f - 44.0f * static_cast<float>(k / 10));
    }
    // §1.2 battle prompt without its shells: the bones of i_attack_mode_choice / _single.alo at the top-anchored origin
    // (0, H - 92) [port: calibrated on 28.jpg].
    const float top = H - kAttackShellTop;
    put("text_attack_choice", 0.0f, top + 84.0f);
    put("choice_button_left", -133.0f, top + 47.0f);
    put("choice_button_right", 132.0f, top + 47.0f);
    put("graph_left", -20.0f, top + 49.0f);
    put("graph_right", 20.0f, top + 49.0f);
    put("text_attack_single", 0.0f, top + 67.0f);
    put("attack_button_single", 7.0f, top + 27.0f);
}

void GalacticHud::init(const ComponentTable& table, const TextureAtlas& atlas, FontCache& fonts, const TextDatabase* text) {
    table_ = &table;
    atlas_ = &atlas;
    fonts_ = &fonts;
    text_ = text;
    buttons_.bind(table);
    auto f = [&](const char* n) { return table.find(n); };
    name_ = f("g_planet_name");
    value_ = f("g_planet_value");
    credit_filter_ = f("b_filters0");
    for (int k = 0; k < 4; ++k) filters_[k] = find_fmt(table, "b_filters%d", k);
    space_tab_ = f("b_space_tab");
    planet_tab_ = f("b_planet_tab");
    dial_ = f("b_dial");
    scroll_left_ = f("b_scroll_left");
    scroll_right_ = f("b_scroll_right");
    build_icon_ = f("g_build");
    smuggled_ = f("g_smuggled");
    corruption_icon_ = f("g_corruption_icon");
    corruption_text_ = f("g_corruption_text");
    enemy_hero_ = f("g_enemy_hero");
    weather_ = f("g_weather");
    ability_ = f("g_planet_ability");
    credit_bar_ = f("g_credit_bar");
    control_ = f("g_political_control");
    ground_ = f("g_ground_level");
    ground_pips_ = f("g_ground_level_pips");
    space_pips_ = f("g_space_level_pips");
    ground_icon_ = f("g_ground_icon");
    land_fleet_text_ = f("Text_Land_Fleet");
    space_ = f("g_space_level");
    conflict_ = f("g_conflict");
    hero_ = f("g_hero");
    smuggler_ = f("g_smuggler");
    bounty_ = f("g_bounty_hunter");
    ring_ = f("g_planet_ring");
    credits_ = f("Text_Credits");
    pop_ = f("Text_Pop_Cap");
    day_ = f("Text_Day_Counter");
    planet_text_ = f("Text_Planet");
    income_ = f("Text_Planetary_Income");
    tech_ = f("Text_Tech_Level");
    day_credits_ = f("Text_Day_Credits");
    planet_info_ = f("planet_info");
    pause_ = f("b_play_pause");
    balance_pip_ = f("balance_pip");
    map_shell_ = f("map_shell");
    special_ = f("g_special_ability");
    obj_back_ = f("objective_back");
    obj_header_ = f("objective_header_text");
    obj_text_ = f("objective_text");
    obj_icon_ = f("objective_icon");
    commandbar_ = f("i_main_commandbar");
    radar_ = f("radar_galactic");
    radar_blip_ = f("g_radar_blip");
    radar_view_ = f("g_radar_view");
    movie_ = f("Movie_galactic");
    planet_fleet_ = f("g_planet_fleet");
    planet_land_ = f("g_planet_land_forces");
    organize_ = f("i_main_organize");
    pending_shell_ = f("pending_battle_shell");
    story_pending_shell_ = f("story_pending_battle_shell");
    choice_text_ = f("text_attack_choice");
    choice_left_ = f("choice_button_left");
    choice_right_ = f("choice_button_right");
    graph_left_ = f("graph_left");
    graph_right_ = f("graph_right");
    single_text_ = f("text_attack_single");
    single_button_ = f("attack_button_single");
    holocron_ = f("b_story_arc_g");
    allegiance_ = f("Text_Allegiance_A");
    build_pad_.clear();
    create_.clear();
    queue_.clear();
    land_.clear();
    fleet_.assign(3, {});
    for (int k = 0; k < 80; ++k)
        if (const int32_t i = find_fmt(table, "build_pad%02d", k); i >= 0) build_pad_.push_back(i);
    for (int k = 0; k < 48; ++k)
        if (const int32_t i = find_fmt(table, "b_Create_%02d", k); i >= 0) create_.push_back(i);
    for (int k = 0; k < 10; ++k)
        if (const int32_t i = find_fmt(table, "queue%02d", k); i >= 0) queue_.push_back(i);
    for (int k = 0; k < 20; ++k)
        if (const int32_t i = find_fmt(table, "land_fleet_%02d", k); i >= 0) land_.push_back(i);
    fleet_header_.clear();
    for (int k = 1; k <= 3; ++k) fleet_header_.push_back(find_fmt(table, "q%d_fleet", k));
    pathfinder_.assign(3, -1);
    for (int fl = 0; fl < 3; ++fl)
        for (int r = 0; r < 5; ++r)
            for (int c = 0; c < 7; ++c) {
                char n[32];
                std::snprintf(n, sizeof n, "q%d_%02d%02d", fl + 1, r, c);
                const int32_t i = table.find(n);
                if (i < 0) continue;
                if (r == 0 && c == 0) pathfinder_[static_cast<size_t>(fl)] = i;  // legacy docs/re/UI.md §7.4: the header pad is no unit pad
                else fleet_[static_cast<size_t>(fl)].push_back(i);
            }
    // the planet map's components (map_shell = planetary_mode.alo).
    map_window_ = f("map_window");
    radar_adv_ = f("radar_galactic_adv_map");
    map_name_ = f("Planet_name_text");
    map_allegiance_ = f("Allegiance_A");
    map_day_ = f("Day_Counter_text");
    map_income_ = f("Planetary_Income_number");
    map_weather_ = f("weather");
    map_bonus_ = f("planet_bonus");
    map_back_ = f("back_button");
    unit_cap_ = f("unit_cap_number");
    building_cap_ = f("building_cap_number");
    map_build_pad_.assign(80, -1);
    map_unit_pad_.assign(10, -1);
    for (int k = 0; k < 80; ++k) map_build_pad_[static_cast<size_t>(k)] = find_fmt(table, "build_pad%02d", k);
    for (int k = 0; k < 10; ++k) map_unit_pad_[static_cast<size_t>(k)] = find_fmt(table, "unit_pad%02d", k);
    map_filters_.clear();
    for (int k = 0; k < 4; ++k) map_filters_.push_back(find_fmt(table, "c_filters%d", k));
    map_texts_ = {f("ground_forces_text"), f("drage_drop_text"), f("unit_cap_text"), f("building_cap_text")};
    dynamic_.assign(table.size(), 0);
    auto mark = [&](int32_t i) {
        if (i >= 0) dynamic_[static_cast<size_t>(i)] = 1;
    };
    mark(map_window_);
    mark(radar_adv_);
    for (const int32_t i : map_unit_pad_) mark(i);
    for (int32_t i : {name_, value_, control_, ground_, space_, conflict_, hero_, smuggler_, bounty_, ring_, planet_text_, obj_back_, obj_header_,
                      obj_text_, obj_icon_, balance_pip_, choice_text_, choice_left_, choice_right_, graph_left_, graph_right_,
                      single_text_, single_button_})
        mark(i);
    for (const auto* v : {&build_pad_, &create_, &queue_, &land_, &fleet_header_})
        for (int32_t i : *v) mark(i);
    for (const auto& v : fleet_)
        for (int32_t i : v) mark(i);
    for (int32_t i : pathfinder_) mark(i);
    // Every g_* label component and the tactical-only components are not command-bar buttons here.
    for (uint32_t i = 0; i < table.size(); ++i) {
        const std::string& n = table[i].name;
        if (n.size() > 2 && (n[0] == 'g' || n[0] == 'G') && n[1] == '_') dynamic_[i] = 1;
    }
}

void GalacticHud::resize(uint32_t width, uint32_t height) {
    resize_canvas(canvas_, fonts_, width, height);
    if (table_) {
        apply_galactic_fallback_layout(*table_, canvas_, layout_);
        layout_.update(canvas_, *table_);
    }
}

void GalacticHud::hit(int32_t component, uint32_t layer, Vec2f center, Vec2f size) {
    hits_.push_back(make_hit_box(component, layer, center, size));
}

int32_t GalacticHud::hit_test(int32_t px, int32_t py) const { return topmost_hit(hits_, canvas_, px, py); }

Vec2f GalacticHud::component_size(uint32_t component, const ComponentPlacement& p) const {
    return component_art_size((*table_)[component], p, *atlas_, {40.0f, 40.0f});  // [port] pads without art
}

int32_t GalacticHud::font_of(int32_t component) const {
    if (component < 0 || !fonts_) return -1;
    const CommandBarComponent& c = (*table_)[static_cast<size_t>(component)];
    return fonts_->component_font(c.font_name, c.font_point_size, FontUse::Text, canvas_.font_height());
}

std::string alternate_font_entry(std::string_view list, int32_t index) {
    int32_t i = 0;
    size_t k = 0;
    while (k < list.size()) {
        while (k < list.size() && (list[k] == ' ' || list[k] == ',' || list[k] == '\t')) ++k;
        const size_t start = k;
        while (k < list.size() && list[k] != ' ' && list[k] != ',' && list[k] != '\t') ++k;
        if (k > start && i++ == index) return std::string(list.substr(start, k - start));
    }
    return {};
}

void GalacticHud::text_at(int32_t component, std::u32string_view text, Vec2f anchor, uint32_t layer, uint8_t alpha,
                          const Rgba8* color, bool second_color, int32_t font) {
    if (component < 0 || text.empty() || !fonts_) return;
    const CommandBarComponent& c = (*table_)[static_cast<size_t>(component)];
    HudTextStyle st;
    st.font = font >= 0 ? font : font_of(component);
    if (st.font < 0) return;
    Rgba8 col = color ? *color : (second_color && c.has_text_color2 ? c.text_color2 : (c.has_text_color ? c.text_color : Rgba8{}));
    st.color = with_alpha(col, alpha);
    st.outline = c.text_outline;
    st.outline_color = with_alpha(st.outline_color, alpha);
    st.emboss = c.text_emboss;
    st.align = c.left_justified ? TextAlign::Left : c.right_justified ? TextAlign::Right : TextAlign::Center;
    add_text(dl_, layer, HudList::Text, *fonts_, canvas_, text, anchor, st);
}

// GALAXY_MAP_PRESENTATION §1.2-1.4: up to three g_planet_fleet buttons around the projected planet - slot 0 at
// (-Default_Offset.x, Default_Offset.y) = (-36, 42), slot 1 at (0, Offset.y) = (0, 47), slot 2 at (36, 42) - and the
// g_planet_land_forces button at (0, Offset.y) = (0, 5); +Y up. Each: the pancake stack fleet_stacks_0n (n = layers,
// fleet_empty for 0) at Size x scale, the representative unit's icon on top (icon bottom = stack bottom + 3 x layers,
// [port] reading of "y = -h/2 + layers x 3"), the stealth / raid badge (Lower_Effect spot). Faction emblems are
// tooltip-only and not drawn.
// Playtest 3 (base 40.jpg): while slots_shown_, every free slot draws the empty oval (Blank_Texture_Name[0] fleet_empty,
// also on fogged planets), clickable as a drop target; the slot under the cursor gets the Mouse_Over_Texture_Name frame
// (i_button_unit_frame_space). The whole fleet being dragged leaves its slot blank (it follows the cursor).
// Base 40.jpg / C2_galactic_silent.png: the 36 x 45 pancake art is drawn at Scale (1.0, ovals 34 units wide); Size.x 0.75 is
// "icon scale" (the XML's own note) and applies to the unit icon only. fleet_empty's fill is opaque in the art and in the
// base shots (same colour over sky, dust and planet): the empty ovals are dark discs tinted like a stack, not outlines.
void GalacticHud::build_planet_stacks(const GalacticPlanetInfo& p, Vec2f at, uint8_t alpha, Vec2f cursor) {
    if (p.stacks.empty() && !slots_shown_) return;
    // Slot rule (§1.3): with the orbit not the local player's, enemy fleets take slot 0 and an own slot-0 fleet moves to
    // slot 2; otherwise fleets keep their slots. Fleets without a slot (or on a taken slot) take the first free one. The
    // slots are the sim's (playtest 3 t07: GalacticPlanetStack::slot = FleetState::slot); a drop only issues commands.
    const GalacticPlanetStack* slot[3] = {nullptr, nullptr, nullptr};
    std::vector<const GalacticPlanetStack*> later;
    for (const GalacticPlanetStack& st : p.stacks) {
        if (st.land) continue;
        if (p.fogged) continue;  // fogged: the ovals only
        int32_t s = st.slot;
        if (!p.orbit_local) s = st.enemy ? 0 : (s == 0 ? 2 : s);
        if (s >= 0 && s < 3 && !slot[s]) slot[s] = &st;
        else later.push_back(&st);
    }
    for (const GalacticPlanetStack* st : later)
        for (auto& sl : slot)
            if (!sl) {
                sl = st;
                break;
            }
    auto hovered = [&](Vec2f c, Vec2f size) {
        return std::fabs(cursor.x - c.x) <= 0.5f * size.x && std::fabs(cursor.y - c.y) <= 0.5f * size.y;
    };
    auto draw = [&](const GalacticPlanetStack& st, int32_t ci, Vec2f c, int32_t slot_index) {
        const CommandBarComponent& cc = (*table_)[static_cast<size_t>(ci)];
        // The whole fleet being dragged: its slot stays blank (Drag_Back draws its pancakes at the cursor).
        if (drag_.active && drag_.unit == 0 && st.fleet != 0 && st.fleet == drag_.fleet && slot_index >= 0) return;
        const float scale = cc.scale;
        const float icon_scale = cc.has_size && cc.size.x > 0.0f ? cc.size.x : 0.75f;
        const float step = cc.offset.x > 0.0f ? cc.offset.x : 3.0f;
        int32_t layers = 0;
        if (st.units > 0) layers = static_cast<int32_t>(std::min<uint32_t>(5u, (st.units + kShipsPerStack - 1) / kShipsPerStack));
        else layers = static_cast<int32_t>(std::min(st.count, 5u));
        const Rgba8 white = with_alpha(Rgba8{}, alpha);
        float bottom = c.y - 14.0f;  // [port] stack art missing: a 28-unit button
        Vec2f hit_size{28.0f, 28.0f};
        if (const std::string* b = pick(cc.blank_texture, layers)) {
            const AtlasEntry* e = atlas_->find(*b);
            if (e) {
                bottom = c.y - 0.5f * static_cast<float>(e->height) * scale;
                hit_size = {static_cast<float>(e->width) * scale, static_cast<float>(e->height) * scale};
            }
            // §1.4 secondary colour = owner faction colour [M: on the stack texture]; the icon and badges stay white.
            add_atlas_quad(dl_, 0, HudList::Alpha, *atlas_, *b, c, {}, scale, with_alpha(Rgba8{st.owner_color.r, st.owner_color.g, st.owner_color.b, 255}, alpha));
        }
        const char* badge = st.stealth ? kStealthBadge : st.raid ? kRaidBadge : nullptr;
        if (badge) add_atlas_quad(dl_, 0, HudList::Alpha, *atlas_, badge, Vec2f{c.x + cc.lower_effect_offset.x, c.y - cc.lower_effect_offset.y}, {}, scale, white);
        if (!st.icon.empty()) {
            const AtlasEntry* e = atlas_->find(st.icon);
            const float ih = e ? static_cast<float>(e->height) * icon_scale : 0.0f;
            add_atlas_quad(dl_, 0, HudList::Alpha, *atlas_, st.icon, {c.x, bottom + step * static_cast<float>(layers) + 0.5f * ih}, {},
                           icon_scale, white);
        }
        const bool hover = slots_shown_ && slot_index >= 0 && hovered(c, hit_size);
        if (st.selected || hover)
            if (const std::string* mo = pick(cc.mouse_over_texture, 0)) add_atlas_quad(dl_, 0, HudList::Alpha, *atlas_, *mo, c, {}, scale, white);
        if (hover) ++stats_.slot_highlights;
        ++stats_.stack_icons;
        // Clickable when the host named the fleet (several instances of one component: the click position picks one).
        if (st.fleet != 0 && st.fleet != commands::kNoObject && alpha > 0) {
            hit(ci, 0, c, hit_size);
            StackHit sh;
            sh.min = {c.x - 0.5f * hit_size.x, c.y - 0.5f * hit_size.y};
            sh.max = {c.x + 0.5f * hit_size.x, c.y + 0.5f * hit_size.y};
            sh.component = ci;
            sh.slot = slot_index;
            sh.fleet = st.fleet;
            sh.planet = p.planet;
            const std::string_view dd = cc.raw_value("DRAG_AND_DROP");
            sh.draggable = !st.land && !st.enemy && data::parse_flag_char(dd);
            sh.enemy = st.enemy;
            sh.layers = layers;
            sh.tint = st.owner_color;
            sh.icon = st.icon;
            stack_hits_.push_back(std::move(sh));
        }
    };
    if (planet_fleet_ >= 0) {
        const CommandBarComponent& fc = (*table_)[static_cast<size_t>(planet_fleet_)];
        const float side_x = fc.default_offset.x != 0.0f ? fc.default_offset.x : 36.0f;
        const float side_y = fc.default_offset.y != 0.0f ? fc.default_offset.y : 42.0f;
        const float mid_y = fc.offset.y != 0.0f ? fc.offset.y : 47.0f;
        const Vec2f pos[3] = {{at.x - side_x, at.y + side_y}, {at.x, at.y + mid_y}, {at.x + side_x, at.y + side_y}};
        for (int k = 0; k < 3; ++k)
            if (slot[k]) draw(*slot[k], planet_fleet_, pos[k], k);
        // Empty slot ovals (fleet_empty) while the slots are shown: drop targets, not draggable, no click.
        if (slots_shown_ && alpha > 0)
            if (const std::string* empty = pick(fc.blank_texture, 0)) {
                const float scale = fc.scale;
                const AtlasEntry* e = atlas_->find(*empty);
                const Vec2f size = e ? Vec2f{static_cast<float>(e->width) * scale, static_cast<float>(e->height) * scale} : Vec2f{27.0f, 20.0f};
                for (int k = 0; k < 3; ++k) {
                    if (slot[k]) continue;
                    add_atlas_quad(dl_, 0, HudList::Alpha, *atlas_, *empty, pos[k], {}, scale,
                                   with_alpha(Rgba8{empty_slot_color_.r, empty_slot_color_.g, empty_slot_color_.b, 255}, alpha));
                    if (hovered(pos[k], size)) {
                        if (const std::string* mo = pick(fc.mouse_over_texture, 0))
                            add_atlas_quad(dl_, 0, HudList::Alpha, *atlas_, *mo, pos[k], {}, scale, with_alpha(Rgba8{}, alpha));
                        ++stats_.slot_highlights;
                    }
                    ++stats_.empty_slots;
                    hit(planet_fleet_, 0, pos[k], size);
                    StackHit sh;
                    sh.min = {pos[k].x - 0.5f * size.x, pos[k].y - 0.5f * size.y};
                    sh.max = {pos[k].x + 0.5f * size.x, pos[k].y + 0.5f * size.y};
                    sh.component = planet_fleet_;
                    sh.slot = k;
                    sh.planet = p.planet;
                    stack_hits_.push_back(std::move(sh));
                }
            }
    }
    if (p.fogged) return;
    if (planet_land_ >= 0) {
        const CommandBarComponent& lc = (*table_)[static_cast<size_t>(planet_land_)];
        for (const GalacticPlanetStack& st : p.stacks)
            if (st.land) {
                draw(st, planet_land_, {at.x, at.y + (lc.offset.y != 0.0f ? lc.offset.y : 5.0f)}, -1);
                break;  // one land-forces button per planet
            }
    }
}

uint32_t GalacticHud::filter_bits() const {
    uint32_t bits = 0;
    for (int k = 0; k < 4; ++k)
        if (filters_[k] >= 0)
            if (const ButtonRuntime* r = buttons_.find_state(static_cast<uint32_t>(filters_[k])); r && r->selected) bits |= 1u << k;
    return bits;
}

void GalacticHud::build_labels(const GalacticHudFrame& f) {
    const float a = planet_label_alpha(f.zoom_state, f.zoom);
    const auto alpha = static_cast<uint8_t>(std::clamp(std::lround(a), 0L, 255L));
    if (alpha == 0) return;
    auto comp = [&](int32_t i) -> const CommandBarComponent* { return i >= 0 ? &(*table_)[static_cast<size_t>(i)] : nullptr; };
    auto up = [](Vec2f o) { return o; };  // [port] label offsets are +Y up
    const Vec2f cursor = canvas_.pixel_to_canvas(f.cursor_px.x + 0.5f, f.cursor_px.y + 0.5f);
    for (size_t pi = 0; pi < f.planets.size(); ++pi) {
        const GalacticPlanetInfo& p = f.planets[pi];
        if (!p.visible) continue;
        float sx = 0, sy = 0;
        if (!project(f.view, p.position, sx, sy)) continue;
        if (sx < -200 || sy < -200 || sx > static_cast<float>(canvas_.screen_width) + 200 ||
            sy > static_cast<float>(canvas_.screen_height) + 200)
            continue;
        const Vec2f at = canvas_.pixel_to_canvas(sx, sy);
        ++stats_.labels;
        // g_planet_ring round every planet, fogged too (C2_galactic_silent.png, ui_005_gc_running.png [H screenshot]): the
        // ellipse is this atlas art at its own size, so all planets get the same one whatever their size or distance. The
        // data says "Only alpha is used to color rings": colour = the name's; 173 / 255 fits both captures on green, blue and
        // grey rings [H screenshot, measured]. Selected planets are marked by the render selection model (§3), not here.
        if (const CommandBarComponent* c = comp(ring_))
            if (const std::string* t = pick(c->icon_texture, 0)) {
                const Rgba8 rc{p.owner_color.r, p.owner_color.g, p.owner_color.b, 173};
                if (add_atlas_quad(dl_, 0, HudList::Alpha, *atlas_, *t, at, {}, c->scale, with_alpha(rc, alpha))) ++stats_.planet_rings;
            }
        // Projected planet radius (canvas units) for the icons inside / around the planet.
        float rad = 30.0f;
        {
            float rx = 0, ry = 0;
            const Vec3 edge{p.position.x + f.view.right.x * p.radius, p.position.y + f.view.right.y * p.radius,
                            p.position.z + f.view.right.z * p.radius};
            if (project(f.view, edge, rx, ry)) rad = std::max(4.0f, std::hypot(rx - sx, ry - sy) * canvas_.units_per_pixel_x);
        }
        if (const CommandBarComponent* c = comp(name_)) {
            const Vec2f o = up(c->offset);
            std::u32string upper = p.name;  // [port: original screenshot] names in capitals
            for (char32_t& ch : upper)
                if (ch >= U'a' && ch <= U'z') ch = static_cast<char32_t>(ch - U'a' + U'A');
            // mcx4 t14 (MCX4_PRES_RE_ANSWERS_3 Q2): the size class's Alternate_Font_Name entry (FoC Medium / Medium /
            // Bold) at the component's own point size; out of range keeps Font_Name.
            const std::string alt = alternate_font_entry(c->alternate_font_name, p.label_class);
            const int32_t lf = alt.empty() || !fonts_ ? -1 : fonts_->component_font(alt, c->font_point_size, FontUse::Button, canvas_.font_height());
            text_at(name_, upper, {at.x + o.x, at.y + o.y}, 0, alpha, &p.owner_color, false, lf);
        }
        if (!p.fogged) {
            // [port: original screenshot] garrison icon inside the planet (0.8 x radius square).
            if (!p.garrison_icon.empty()) {
                const float s = 0.8f * rad;
                add_atlas_quad(dl_, 0, HudList::Alpha, *atlas_, p.garrison_icon, at, {s, s}, 1.0f, with_alpha(Rgba8{}, alpha), {s, s});
                ++stats_.garrison_icons;
            }
        }
        build_planet_stacks(p, at, alpha, cursor);
        const uint32_t filters = filter_bits();
        const bool f_credit = (filters & kFilterCredit) != 0, f_level = (filters & kFilterBaseLevel) != 0,
                   f_weather = (filters & kFilterAbilityWeather) != 0;
        const float value_dy = value_ >= 0 ? (*table_)[static_cast<size_t>(value_)].offset.y : -11.0f;
        // §1.7 (0x3ea1b0): b_filters0 shows "+N" (owner colour); the only credit read-out on the map.
        if (const CommandBarComponent* c = comp(value_); c && p.credits >= 0 && f_credit) {
            const Vec2f o = up(c->offset);
            text_at(value_, U"+" + num32(p.credits), {at.x + o.x, at.y + o.y}, 0, alpha, &p.owner_color);
            ++stats_.planet_values;
        }
        // §1.7: b_filters2 shows the weather (g_weather, Offset -20 -31) and ability (g_planet_ability, Offset 0 -34) icons;
        // no visibility gate.
        if (f_weather)
            for (auto [ci, tex, n] : {std::tuple{weather_, &p.weather_icon, &stats_.weather_icons}, std::tuple{ability_, &p.ability_icon, &stats_.ability_icons}}) {
                const CommandBarComponent* c = comp(ci);
                if (!c || tex->empty()) continue;
                const Vec2f o = up(c->offset);
                if (add_atlas_quad(dl_, 0, HudList::Alpha, *atlas_, *tex, {at.x + o.x, at.y + o.y}, {}, c->scale, with_alpha(c->color, alpha), {16, 16}))
                    ++*n;
            }
        // §1.7: g_corruption_text (Offset 30 15) whenever a corruption transition runs.
        if (const CommandBarComponent* c = comp(corruption_text_); c && !p.corruption_text.empty()) {
            const Vec2f o = up(c->offset);
            text_at(corruption_text_, p.corruption_text, {at.x + o.x, at.y + o.y}, 0, alpha);
            ++stats_.corruption_texts;
        }
        // §1.7 (0x3db950): the level pip rows, only with b_filters1. Pip i < max is drawn, in the owner colour while i < the
        // level, else at half intensity; the row starts at the pips' Default_Offset (space 34 -13, ground 34 -26), one pip
        // width per step, lowered by the value text's Offset y while b_filters0 is on.
        if (f_level)
            for (auto [ci, level, max] : {std::tuple{space_pips_, p.starbase_info ? std::max(0, p.space_level) : 0, p.space_max},
                                          std::tuple{ground_pips_, p.groundbase_info ? std::max(0, p.ground_level) : 0, p.ground_max}}) {
                const CommandBarComponent* c = comp(ci);
                if (!c || max <= 0) continue;
                const std::string* t = c->icon_alternate_texture.empty() ? pick(c->icon_texture, 0) : pick(c->icon_alternate_texture, 0);
                if (!t) continue;
                const AtlasEntry* e = atlas_->find(*t);
                const float w = e ? static_cast<float>(e->width) : 6.0f;
                const Vec2f o = up(c->default_offset);
                const float y = at.y + o.y + (f_credit ? value_dy : 0.0f);
                const Rgba8 full = p.owner_color;
                const Rgba8 half{static_cast<uint8_t>(full.r / 2), static_cast<uint8_t>(full.g / 2), static_cast<uint8_t>(full.b / 2), full.a};
                for (int32_t i = 0; i < max; ++i)
                    if (add_atlas_quad(dl_, 0, HudList::Alpha, *atlas_, *t, {at.x + o.x + w * static_cast<float>(i), y}, {}, c->scale,
                                       with_alpha(i < level ? full : half, alpha)))
                        ++stats_.level_pips;
            }
        // §1.7 status row (0x3dbc00): from g_conflict's Offset, lowered by the value text's Offset y with b_filters0 and
        // g_ground_icon's Default_Offset y with b_filters1; the visible icons left to right, each centred half its width
        // on, the pen moving by its width.
        Vec2f pen = conflict_ >= 0 ? up(comp(conflict_)->offset) : Vec2f{30, -16};
        if (f_credit) pen.y += value_dy;
        if (f_level && ground_icon_ >= 0) pen.y += up(comp(ground_icon_)->default_offset).y;
        pen = {at.x + pen.x, at.y + pen.y};
        auto icon = [&](int32_t ci, bool on, int32_t alt, const std::string* tex = nullptr) {
            const CommandBarComponent* c = comp(ci);
            if (!c || !on) return;
            const std::string* t = tex && !tex->empty() ? tex
                                   : c->icon_texture.empty() ? pick(c->icon_alternate_texture, alt) : pick(c->icon_texture, 0);
            if (!t) return;
            const AtlasEntry* e = atlas_->find(*t);
            const float w = e ? static_cast<float>(e->width) : 12.0f;
            add_atlas_quad(dl_, 0, HudList::Alpha, *atlas_, *t, {pen.x + 0.5f * w, pen.y}, {}, c->scale, with_alpha(c->color, alpha), {12, 12});
            pen.x += w;
            ++stats_.label_icons;
        };
        icon(conflict_, p.conflict, 0);
        if (f.simplified_colors) {  // #468 (0x3ec380 [H]): g_hero "you" and g_enemy_hero instead of the faction medals
            icon(hero_, p.hero_you, 0);
            icon(enemy_hero_, p.enemy_hero >= 0, std::max(p.enemy_hero, 0));
        } else {
            icon(hero_, p.hero_rebel, 2);  // g_hero alternates: you, enemy, rebel, empire, underworld
            icon(hero_, p.hero_empire, 3);
            icon(hero_, p.hero_underworld, 4);
        }
        icon(build_icon_, p.building, 0);
        icon(smuggler_, p.smuggler, 0);
        icon(bounty_, p.bounty_hunter, 0);
        icon(smuggled_, p.smuggled, 0);
        icon(corruption_icon_, !p.corruption_icon.empty() && (!p.corrupted || f_weather), 0, &p.corruption_icon);
    }
}

void GalacticHud::set_build_tab(bool space) {
    if (space == build_space_tab_) return;  // setting the current tab again is silent
    build_space_tab_ = space;
    buttons_.play_sfx("GUI_Dial_Switch_SFX", space ? space_tab_ : planet_tab_);
}

void GalacticHud::build_bar(const GalacticHudFrame& f) {
    // ui4 t12 (GALACTIC_DIALOGS §5.1.1, 0x3e8a80): an inaccessible surface forces the space tab and disables the land tab;
    // the tab buttons show the current tab Selected, the dial its arrow (land: Icon_Alternate 1 i_button_dial_right,
    // space: 0 i_button_dial_left).
    land_tab_allowed_ = f.land_tab_allowed;
    if (!land_tab_allowed_) set_build_tab(true);
    if (planet_tab_ >= 0) {
        ButtonRuntime& r = buttons_.state(static_cast<uint32_t>(planet_tab_));
        r.selected = !build_space_tab_;
        r.disabled = !land_tab_allowed_;
    }
    if (space_tab_ >= 0) buttons_.state(static_cast<uint32_t>(space_tab_)).selected = build_space_tab_;
    if (dial_ >= 0) buttons_.state(static_cast<uint32_t>(dial_)).alt = build_space_tab_ ? 0 : 1;
    if (land_fleet_text_ >= 0) {  // the organiser's "Base Layout" caption
        ButtonRuntime& r = buttons_.state(static_cast<uint32_t>(land_fleet_text_));
        r.hidden = f.planet_destroyed;
        r.text = text_or("TEXT_BASE_LAYOUT", U"Base Layout");
        r.text2 = f.ground_forces_unknown ? text_or("TEXT_UNKNOWN", U"Unknown") : std::u32string();
    }
    auto set_text = [&](int32_t ci, std::u32string t) {
        if (ci >= 0) buttons_.state(static_cast<uint32_t>(ci)).text = std::move(t);
    };
    // [port: Gabriel, original screenshot] "Day: 1" (TEXT_CYCLES), "Tech: 3" (TEXT_TECH), "13,000", income in
    // Text_Day_Credits (TEXT_TOOLTIP_TOTAL_INCOME).
    auto label = [&](const char* key, const char32_t* fallback, int64_t v, const char32_t* sep) {
        std::u32string t = text_or(key, fallback);
        while (!t.empty() && t.back() == U' ') t.pop_back();
        if (!t.empty() && t.back() != U':') t.push_back(U':');
        return t + sep + num32(v);
    };
    // [port: Gabriel, original screenshot] the credit amounts (reserve, income, planet income) start with the credits
    // symbol, which is the '$' glyph of the EmpireAtWar fonts these components use (register_game_fonts).
    set_text(credits_, f.credits != kNoGalacticAmount ? kCreditsPrefix + grouped32(f.credits) : std::u32string());
    set_text(day_credits_, f.income != kNoGalacticAmount ? kCreditsPrefix + grouped32(f.income) : std::u32string());
    if (f.pop >= 0) {
        std::u32string t = num32(f.pop);
        if (f.pop_cap >= 0) t += U" / " + num32(f.pop_cap);
        set_text(pop_, std::move(t));
    } else {
        set_text(pop_, {});
    }
    set_text(day_, f.day >= 0 ? label("TEXT_CYCLES", U"Day", f.day, U" ") : std::u32string());
    set_text(tech_, f.tech >= 0 ? label("TEXT_TECH", U"Tech", f.tech, U" ") : std::u32string());
    const GalacticPlanetInfo* sel = selected_ >= 0 ? &f.planets[static_cast<size_t>(selected_)] : nullptr;
    // GALACTIC_DIALOGS §5.3 (25.jpg): planet name in capitals and planet income, both in the owner's colour; the owner's
    // emblem on Text_Allegiance_A; the credits symbol is the font's '$' (blue like the number in 25.jpg, so not the fixed
    // yellow i_icon_credit.tga).
    std::u32string pname = sel ? sel->name : std::u32string();
    for (char32_t& ch : pname)
        if (ch >= U'a' && ch <= U'z') ch = static_cast<char32_t>(ch - U'a' + U'A');
    set_text(planet_text_, std::move(pname));
    set_text(income_, sel && sel->income != kNoGalacticAmount ? kCreditsPrefix + grouped32(sel->income) : std::u32string());
    // D12 (25.jpg, bone (175, 257)): the name's line is centred on the bone, inside the top strip. Drawn here because the generic
    // button text hangs a Max_Text_Width block below its anchor, half a line low: onto the Day bar. Bold 9, outlined and the
    // owner's colour are as in 25.jpg and the XML.
    if (planet_text_ >= 0 && sel && fonts_ && !map_open_ && !overlay_) {
        const auto u = static_cast<uint32_t>(planet_text_);
        const ComponentPlacement& p = layout_.placement(u);
        const CommandBarComponent& c = (*table_)[u];
        const std::u32string& name = buttons_.state(u).text;
        HudTextStyle st;
        st.font = font_of(planet_text_);
        if (p.placed && st.font >= 0) {
            st.color = Rgba8{sel->owner_color.r, sel->owner_color.g, sel->owner_color.b, 255};
            st.outline = c.text_outline;
            st.align = TextAlign::Center;
            // t47 [port]: one line, narrowed into Max_Text_Width; a wrapped name covered the Day row below.
            st.width_scale = fit_width_scale(measure_text_block(*fonts_, canvas_, name, 0.0f, st).width, c.max_text_width);
            const TextBlock tb = measure_text_block(*fonts_, canvas_, name, 0.0f, st);
            add_text_block(dl_, layer_of(c, p), HudList::Text, *fonts_, canvas_, name, {p.center.x, p.center.y + 0.5f * tb.height}, 0.0f, st);
        }
    }
    for (const int32_t ci : {planet_text_, income_}) {
        if (ci < 0) continue;
        ButtonRuntime& r = buttons_.state(static_cast<uint32_t>(ci));
        r.text_color_set = sel != nullptr;
        if (sel) r.text_color = Rgba8{sel->owner_color.r, sel->owner_color.g, sel->owner_color.b, 255};
    }
    if (allegiance_ >= 0) {
        ButtonRuntime& r = buttons_.state(static_cast<uint32_t>(allegiance_));
        r.hidden = !sel || sel->allegiance < 0;
        r.alt = sel ? sel->allegiance : -1;
    }
    // §2.1: the holocron flashes while a chapter is unread.
    if (holocron_ >= 0) buttons_.state(static_cast<uint32_t>(holocron_)).flash = f.holocron_flash;
    // §5.4: planet_info only on a shell bone (organiser / map_shell) with a planet selected - never at a fallback spot.
    if (planet_info_ >= 0)
        buttons_.state(static_cast<uint32_t>(planet_info_)).hidden =
            sel == nullptr || !layout_.placement(static_cast<uint32_t>(planet_info_)).from_bone;
    // 26.jpg: the button caption "Planet Info" (its tooltip key) and the organiser tabs "Fleet 1..3" (TEXT_FLEET_PANE_0..2).
    set_text(planet_info_, text_or("TEXT_BUTTON_PLANET_INFO", U"Planet Info"));
    // [port: 26.jpg] the tab labels start at their bone (Text_Fleet1 x -471.4 = the text's left edge) with the line's
    // vertical centre on it, so they are drawn here rather than by the generic (centred, wrapped) button text.
    for (int k = 0; k < 3 && fonts_; ++k) {
        const int32_t ci = find_fmt(*table_, "text_fleet%d", k + 1);
        if (ci < 0) continue;
        dynamic_[static_cast<size_t>(ci)] = 1;
        const ComponentPlacement& p = layout_.placement(static_cast<uint32_t>(ci));
        if (!p.from_bone || !stats_.organize_shown) continue;
        char key[32];
        std::snprintf(key, sizeof key, "TEXT_FLEET_PANE_%d", k);
        const CommandBarComponent& c = (*table_)[static_cast<size_t>(ci)];
        HudTextStyle st;
        st.font = font_of(ci);
        if (st.font < 0) continue;
        st.color = c.has_text_color ? c.text_color : Rgba8{0, 0, 0, 255};
        st.align = TextAlign::Left;
        add_text(dl_, layer_of(c, p), HudList::Text, *fonts_, canvas_, text_or(key, U"Fleet " + num32(k + 1)), p.center, st);
    }

    // Generic buttons / texts of the placed components (shell bones or fallbacks).
    for (uint32_t i = 0; i < table_->size(); ++i) {
        if (dynamic_[i]) continue;
        const CommandBarComponent& c = (*table_)[i];
        if (c.type != ComponentType::Button && c.type != ComponentType::TextButton && c.type != ComponentType::Icon) continue;
        const ComponentPlacement& p = layout_.placement(i);
        if (!p.placed) continue;
        if ((map_open_ || overlay_) && !p.from_bone) continue;  // the command bar is hidden; no fallback-placed component shows
        const ButtonRuntime* r = buttons_.find_state(i);
        if (r ? r->hidden : c.hidden) continue;
        const bool has_art = !c.blank_texture.empty() || !c.icon_texture.empty() || !c.icon_alternate_texture.empty() ||
                             (r && (!r->icon_override.empty() || !r->text.empty()));
        if (!has_art && c.tooltip_text.empty()) continue;
        const uint32_t layer = layer_of(c, p);
        Vec2f size = buttons_.draw(dl_, *atlas_, fonts_, canvas_, i, p.center, p.size, f.faction_alt, f.time,
                                   static_cast<int32_t>(layer) - c.base_layer);
        if (p.size.x > 0) size = p.size;
        hit(static_cast<int32_t>(i), layer, p.size.x > 0 ? p.rect_center : p.center, size);
        ++stats_.buttons;
    }

    // Build pads (selected planet), GALACTIC_DIALOGS §5.1.1 (0x3145c0): only the current tab's options; GUI_Row 0 fills the
    // odd pads b_Create_01, 03 .. (lower row), any other GUI_Row the even pads b_Create_00, 02 .. (upper row), each from
    // the left in provider order; past a row's pads the options are dropped (no scrolling: b_scroll_left / _right stay
    // hidden). The shell has no build_padNN bones, so those components stay unused here.
    if (map_open_ || overlay_) {  // ui4 t04 (GALACTIC_PLANET_MAP §1.2): no build pads or queue under the planet map (#593: nor under Modern)
        for (const auto* v : {&create_, &queue_})
            for (const int32_t ci : *v) buttons_.state(static_cast<uint32_t>(ci)).hidden = true;
        return;
    }
    std::vector<GalacticBuildOption> options;
    if (sel && f.providers.build_options) f.providers.build_options(sel->planet, options);
    for (const int32_t ci : {scroll_left_, scroll_right_})
        if (ci >= 0) buttons_.state(static_cast<uint32_t>(ci)).hidden = true;
    std::vector<uint8_t> pad_used(create_.size(), 0);
    size_t row_next[2] = {1, 0};  // row 0 -> odd pads, row 1 -> even pads
    auto pad = [&](int32_t ci, PadSlot slot) {
        const auto u = static_cast<uint32_t>(ci);
        const ComponentPlacement& p = layout_.placement(u);
        ButtonRuntime& r = buttons_.state(u);
        r.hidden = false;
        if (!p.placed) return false;
        const CommandBarComponent& c = (*table_)[u];
        const uint32_t layer = layer_of(c, p);
        const Vec2f sz = buttons_.draw(dl_, *atlas_, fonts_, canvas_, u, p.center, p.size, f.faction_alt, f.time,
                                       static_cast<int32_t>(layer) - c.base_layer);
        hit(ci, layer, p.size.x > 0 ? p.rect_center : p.center, p.size.x > 0 ? p.size : (sz.x > 0 ? sz : component_size(u, p)));
        slot.component = ci;
        pads_.push_back(std::move(slot));
        return true;
    };
    for (const GalacticBuildOption& o : options) {
        if (o.space != build_space_tab_) continue;
        size_t& next = row_next[o.row == 0 ? 0 : 1];
        if (next >= create_.size()) continue;  // the row is full: dropped
        pad_used[next] = 1;
        const int32_t ci = create_[next];
        next += 2;
        ButtonRuntime& r = buttons_.state(static_cast<uint32_t>(ci));
        r.icon_override = o.icon;
        r.disabled = !o.enabled || o.production_disabled || o.block != HudBuildOption::Block::None;
        // the Tooltip_Text entry by block reason (as the tactical Build_NN pads); an enabled pad has none.
        switch (o.block) {
            case HudBuildOption::Block::QueueFull: r.tooltip_index = 0; break;
            case HudBuildOption::Block::PopCap: r.tooltip_index = 1; break;
            case HudBuildOption::Block::Funds: r.tooltip_index = 2; break;
            default: r.tooltip_index = -1; break;
        }
        // GALACTIC_DIALOGS §5.1 second line (Text_Offset2 0 -20, Text_Color2 gold) = the queued count [L; port: > 0].
        r.text2 = o.queued > 0 ? num32(static_cast<int32_t>(o.queued)) : std::u32string();
        r.alt = build_space_tab_ ? 1 : 0;  // §5.1.1: Blank_Texture_Name[tab] (amber land, blue space)
        PadSlot s;
        s.option = o;
        if (!pad(ci, std::move(s))) continue;
        ++stats_.build_buttons;
        // §5.1 (25.jpg): the cost centred along the pad's bottom edge, inside it; green when affordable [port: red when
        // not, L].
        if (o.cost >= 0 && fonts_) {
            const auto u = static_cast<uint32_t>(ci);
            const ComponentPlacement& p = layout_.placement(u);
            const CommandBarComponent& c = (*table_)[u];
            const Vec2f box = p.size.x > 0 ? p.size : component_size(u, p);
            const Vec2f mid = p.size.x > 0 ? p.rect_center : p.center;
            HudTextStyle st;
            st.font = font_of(ci);
            if (st.font < 0) continue;
            // the colour says affordable or not (Funds, or a host without a block reason that disabled it).
            const bool unaffordable = o.production_disabled || o.block == HudBuildOption::Block::Funds ||
                                      (!o.enabled && o.block == HudBuildOption::Block::None);
            st.color = unaffordable ? kCostUnaffordable : kCostAffordable;
            st.outline = c.text_outline;
            st.align = TextAlign::Center;
            add_text(dl_, layer_of(c, p), HudList::Text, *fonts_, canvas_, num32(o.cost), {mid.x, mid.y - 0.5f * box.y + 7.0f}, st);
        }
    }
    for (const int32_t ci : build_pad_) buttons_.state(static_cast<uint32_t>(ci)).hidden = true;
    for (size_t k = 0; k < create_.size(); ++k)
        if (!pad_used[k]) buttons_.state(static_cast<uint32_t>(create_[k])).hidden = true;

    // Build queue.
    std::vector<HudQueueEntry> queue;
    if (f.providers.build_queue) f.providers.build_queue(queue);
    // GALACTIC_DIALOGS §5.2: space entries on the blue domes queue00..04 (Build_Queue), ground entries on the orange domes
    // queue05..09 (Build_Queue_Land), each a hologram cone rising from its dome with the unit icon and the build dial.
    const size_t half = (queue_.size() + 1) / 2;
    size_t used[2] = {0, 0};
    std::vector<const HudQueueEntry*> slot_entry(queue_.size(), nullptr);
    for (const HudQueueEntry& e : queue) {
        const size_t side = e.land ? 1 : 0;
        const size_t base = side == 0 ? 0 : half, cap = side == 0 ? half : queue_.size() - half;
        if (used[side] >= cap) continue;
        slot_entry[base + used[side]++] = &e;
    }
    for (size_t k = 0; k < queue_.size(); ++k) {
        const auto u = static_cast<uint32_t>(queue_[k]);
        ButtonRuntime& r = buttons_.state(u);
        if (!slot_entry[k]) {
            r.hidden = true;
            continue;
        }
        const HudQueueEntry& e = *slot_entry[k];
        r.icon_override = e.icon;
        r.build_fill = std::clamp(e.progress, 0.0f, 1.0f) * 100.0f;
        r.text = e.count > 1 ? num32(e.count) : std::u32string();
        PadSlot s;
        s.is_queue = true;
        s.queue = e;
        if (pad(queue_[k], std::move(s))) ++stats_.queue_entries;
    }
}

void GalacticHud::build_fleets(const GalacticHudFrame& f) {
    const GalacticPlanetInfo* sel = selected_ >= 0 ? &f.planets[static_cast<size_t>(selected_)] : nullptr;
    std::vector<GalacticFleet> fleets;
    std::vector<GalacticFleetSlot> land;
    if (sel && stats_.organize_shown) {
        if (f.providers.fleets) f.providers.fleets(sel->planet, fleets);
        if (f.providers.land_forces) f.providers.land_forces(sel->planet, land);
    }
    // legacy docs/re/UI.md §7.2: pane k = frame q{k+1}; a fleet's pane comes from its orbit slot, not from the provider order
    // (galactic_organiser_panes, #613 shared with the Modern organiser). The same rule as the map's build_planet_stacks (§1.3).
    constexpr size_t kPanes = kOrganiserPanes;
    GalacticOrganiserPanes panes;
    galactic_organiser_panes(fleets, sel && sel->orbit_local, panes);
    auto draw_slot = [&](int32_t ci, const GalacticFleetSlot& s, std::vector<uint32_t> select, int32_t fleet_index, uint32_t fleet,
                         uint32_t unit, bool foreign, bool empty = false) {
        const auto u = static_cast<uint32_t>(ci);
        ButtonRuntime& r = buttons_.state(u);
        r.hidden = false;
        r.disabled = false;  // the planet map may have left land_fleet_NN disabled
        r.icon_override = s.icon;
        r.selected = s.selected;
        r.text = s.count > 1 ? num32(s.count) : std::u32string();
        r.tooltip_text = s.name;  // [port] a player-given fleet / ship name
        // #767 0x3095c0 [H]: a pad that shows a unit a drop moved (away from its source fleet) scales up, once per unit.
        if (r.scale_up_start >= 0.0 && f.time - r.scale_up_start > 1.0) r.scale_up_start = -1.0;  // long done
        for (auto it = pops_.begin(); it != pops_.end();) {
            if (it->from_fleet != fleet && std::find(select.begin(), select.end(), it->unit) != select.end()) {
                r.scale_up_start = f.time;
                it = pops_.erase(it);
            } else {
                ++it;
            }
        }
        ghost_dragged_pad(ci, static_cast<int32_t>(s.count), r);
        const ComponentPlacement& p = layout_.placement(u);
        if (!p.placed) return false;
        const CommandBarComponent& c = (*table_)[u];
        const uint32_t layer = layer_of(c, p);
        const Vec2f sz = buttons_.draw(dl_, *atlas_, fonts_, canvas_, u, p.center, p.size, f.faction_alt, f.time,
                                       static_cast<int32_t>(layer) - c.base_layer);
        hit(ci, layer, p.size.x > 0 ? p.rect_center : p.center, p.size.x > 0 ? p.size : (sz.x > 0 ? sz : component_size(u, p)));
        PadSlot ps;
        ps.component = ci;
        ps.select = std::move(select);
        const std::string_view dd = c.raw_value("DRAG_AND_DROP");
        ps.draggable = !foreign && !empty && data::parse_flag_char(dd);
        ps.foreign = foreign;
        ps.fleet_index = fleet_index;
        ps.fleet = fleet;
        ps.unit = unit;
        ps.empty = empty;
        ps.icon = s.icon;
        ps.name = s.name;
        ps.no_lead = s.no_lead;
        pads_.push_back(std::move(ps));
        return true;
    };
    // #515: the panes belong to the organiser. While it is closed (the galaxy view, the planet map) every pane component is
    // hidden and takes no hit, a drag included; otherwise its pads drew, frameless, over the galaxy.
    auto hide = [&](int32_t ci) {
        if (ci >= 0) buttons_.state(static_cast<uint32_t>(ci)).hidden = true;
    };
    for (std::vector<uint32_t>& v : pane_leads_) v.clear();
    // #767 [port] (tier 1, presentation: docs/SIMULATION.md "Parity tiers") a move not seen on a pad within 5 s expires.
    std::erase_if(pops_, [&f](const PadPop& p) { return f.time - p.since > 5.0; });
    if (!stats_.organize_shown) {
        for (size_t fl = 0; fl < fleet_.size(); ++fl) {
            for (const int32_t ci : fleet_[fl]) hide(ci);
            if (fl < fleet_header_.size()) hide(fleet_header_[fl]);
            if (fl < pathfinder_.size()) hide(pathfinder_[fl]);
        }
    }
    for (size_t fl = 0; stats_.organize_shown && fl < fleet_.size() && fl < kPanes; ++fl) {  // all three panes always while open: the frames and tabs are shell geometry (§7.1)
        const std::vector<int32_t>& slots = fleet_[fl];
        const std::vector<const GalacticFleet*>& in_pane = panes[fl];
        const GalacticFleet* fd = in_pane.empty() ? nullptr : in_pane.front();
        const auto fi = static_cast<int32_t>(fl);
        // Fleet header (q{n}_fleet): the faction's big fleet icon of the pane's first fleet; dragging it drags the whole fleet
        // (not for a foreign one, whose header is shown but inert) [port].
        if (fl < fleet_header_.size() && fleet_header_[fl] >= 0) {
            if (fd) {
                GalacticFleetSlot hs;
                hs.object = fd->fleet;
                hs.name = fd->name;
                draw_slot(fleet_header_[fl], hs, fd->enemy ? std::vector<uint32_t>{} : std::vector<uint32_t>{fd->fleet}, fi, fd->fleet, 0, fd->enemy);
            } else {
                buttons_.state(static_cast<uint32_t>(fleet_header_[fl])).hidden = true;
            }
        }
        const uint32_t pane_fleet = fd ? fd->fleet : 0;
        const bool pane_foreign = fd && fd->enemy;
        // legacy docs/re/UI.md §7.4 (#515): q{n}_0000 ("Fleet PF", the pathfinder) is the header pad, not a unit pad; the unit pads
        // are q{n}_0001 upward. It is always shown: the pane's first lead unit (stack builder 0x3cac60: that unit leaves its
        // stack), else empty (its blank texture). An own pane's header pad is a drop target (lead_drop); a filled one drags
        // its unit like any unit pad. [port] a further lead in the pane is drawn in its stack (retail resets it to -1 here):
        // lead_drop clears every other lead of the pane, so only a merge of two led fleets leaves one; presentation, tier 3.
        const GalacticFleet* lead_fleet = nullptr;
        const GalacticFleetSlot* lead = nullptr;
        for (const GalacticFleet* gf : in_pane) {
            if (!gf->enemy)
                for (const GalacticFleetSlot& u : gf->units)
                    if (u.lead && u.object != 0) pane_leads_[fl].push_back(u.object);
            if (!lead && (lead = galactic_fleet_lead(gf->units)) != nullptr) lead_fleet = gf;
        }
        if (pathfinder_[fl] >= 0) {
            const bool drawn = lead ? draw_slot(pathfinder_[fl], *lead, lead_fleet->enemy ? std::vector<uint32_t>{} : std::vector<uint32_t>{lead->object}, fi,
                                                lead_fleet->fleet, lead->object, lead_fleet->enemy)
                                    : draw_slot(pathfinder_[fl], GalacticFleetSlot{}, {}, fi, pane_fleet, 0, pane_foreign, true);
            if (drawn) {
                pads_.back().lead_pad = true;
                pads_.back().no_lead = lead && lead->no_lead;
            }
        }
        // legacy docs/re/UI.md §7.2: the pane's units, grouped per fleet and type into stacks (one pad each, the count as text), fill the unit
        // pads upward, fleet after fleet; further stacks are not drawn. Dragging a stack takes its first unit.
        struct Stack {
            const GalacticFleet* fleet;
            GalacticFleetSlot slot;  // the first unit, count summed
            std::vector<uint32_t> members;
        };
        std::vector<Stack> stacks;
        std::vector<GalacticUnitStack> of_fleet;
        for (const GalacticFleet* gf : in_pane) {
            of_fleet.clear();
            galactic_unit_stacks(gf->units, lead_fleet == gf ? lead : nullptr, of_fleet);  // #515: the lead is on the header pad
            for (GalacticUnitStack& s : of_fleet) stacks.push_back({gf, std::move(s.slot), std::move(s.members)});
        }
        size_t next = 0;
        for (const Stack& st : stacks) {
            if (next >= slots.size()) break;
            if (draw_slot(slots[next], st.slot, st.fleet->enemy ? std::vector<uint32_t>{} : st.members, fi, st.fleet->fleet, st.slot.object,
                          st.fleet->enemy))
                ++stats_.fleet_slots;
            ++next;
        }
        // legacy docs/re/UI.md §7.3: the pads without a stack show (empty, as drop targets) only while something is dragged.
        for (; next < slots.size(); ++next) {
            if (drag_.active) draw_slot(slots[next], GalacticFleetSlot{}, {}, fi, pane_fleet, 0, pane_foreign, true);
            else buttons_.state(static_cast<uint32_t>(slots[next])).hidden = true;
        }
    }
    for (size_t k = 0; k < land_.size() && !map_open_; ++k) {  // the planet map owns land_fleet_NN while open
        if (k >= land.size()) {
            buttons_.state(static_cast<uint32_t>(land_[k])).hidden = true;
            continue;
        }
        if (draw_slot(land_[k], land[k], {land[k].object}, -1, 0, land[k].object, false)) ++stats_.land_slots;
    }
}

// ---- fleet drag and drop ----------------------------------------------------------------------------------------
// The slot a new fleet should take (payload `b`, galactic campaign.hpp "kFleetSlot").
static commands::EventData with_slot_request(commands::EventData e, int32_t slot) {
    if (slot >= 0) e.set("b", static_cast<uint32_t>(slot));
    return e;
}

// The order a drop on a fleet slot issues (rules: galactic_hud.hpp, "Fleet-slot drops"); out = nullptr only tests it.
bool galactic_slot_drop(const GalacticOrganiserDrag& d, const GalacticDropTarget& sh, uint32_t selected,
                        std::vector<commands::EventData>* out) {
    constexpr uint32_t kNone = commands::kNoObject;
    const bool land = !d.from_fleet;
    const uint32_t home = d.planet != 0 ? d.planet : selected;  // the dragged fleet's / unit's planet
    const uint32_t unit = d.unit != 0 ? d.unit : kNone;
    const bool empty = sh.fleet == 0 || sh.fleet == kNone;
    if (sh.planet == 0 || home == 0) return false;
    if (land && d.unit == 0) return false;
    if (sh.planet == home) {
        if (!empty) {
            if (sh.foreign || sh.fleet == d.fleet) return false;
            if (!out) return true;
            // #763: a Ctrl drop of a Stackable pad merges / launches every unit of its stack (0x3e0eb0 into that fleet).
            const std::vector<uint32_t> one{unit};
            for (const uint32_t u : d.unit != 0 && !d.stack.empty() ? d.stack : one)
                out->push_back(land ? fleet_management_event(commands::kFleetLaunch, sh.fleet, kNone, u)
                                    : fleet_management_event(commands::kFleetMerge, sh.fleet, d.fleet, u));
            return true;
        }
        if (!land && d.unit == 0) {  // re-slot the whole fleet
            if (out) out->push_back(fleet_management_event(commands::kFleetSlot, home, d.fleet, kNone, kNone, static_cast<uint8_t>(sh.slot)));
            return true;
        }
        if (out) {
            // The unit becomes a new fleet in that slot: detach from its fleet, or launch from the surface (op 6 with the
            // destination = its own planet); the slot rides in the payload's `b` and the sim gives it to the new fleet.
            out->push_back(with_slot_request(land ? fleet_management_event(commands::kFleetMoveNew, home, kNone, unit, home)
                                                  : fleet_management_event(commands::kFleetDetach, home, d.fleet, unit),
                                             sh.slot));
        }
        return true;
    }
    // Another planet: a move order through the command stream (the sim splits a unit off its fleet).
    if (out) {
        // [inferred] the slot chosen (an empty one, or an own fleet's: merge on arrival) is the sim's to apply on arrival;
        // an enemy fleet's slot is no choice.
        const bool choose = sh.slot >= 0 && (empty || !sh.foreign);
        if (land || d.unit != 0) {  // the unit becomes a new fleet that moves (op 6; `b` = its slot at the destination)
            commands::EventData e = fleet_management_event(commands::kFleetMoveNew, home, land ? kNone : d.fleet, unit, sh.planet);
            out->push_back(choose ? with_slot_request(std::move(e), sh.slot) : std::move(e));
        } else {
            if (choose)  // op 8 with a destination planet: the slot it takes on arrival, ahead of the move order
                out->push_back(fleet_management_event(commands::kFleetSlot, home, d.fleet, kNone, sh.planet, static_cast<uint8_t>(sh.slot)));
            out->push_back(commands::ev::move_object_to_object(d.fleet, sh.planet));
        }
    }
    return true;
}

bool GalacticHud::slot_drop(const StackHit& sh, std::vector<commands::EventData>* out) {
    GalacticDropTarget t{GalacticDropTarget::Kind::Slot, sh.fleet, sh.enemy};
    t.planet = sh.planet;
    t.slot = sh.slot;
    return galactic_slot_drop({drag_.fleet_index >= 0, drag_.fleet, drag_.unit, drag_.planet}, t, selected_id_, out);
}

void galactic_organiser_panes(std::span<const GalacticFleet> fleets, bool orbit_local, GalacticOrganiserPanes& panes) {
    for (auto& pane : panes) pane.clear();
    std::vector<const GalacticFleet*> later;
    for (const GalacticFleet& gf : fleets) {
        std::vector<const GalacticFleet*>* to = nullptr;
        if (orbit_local) {
            if (gf.enemy) continue;  // §7.2 first row: only the local player's fleets fill the panes
            if (gf.slot == 0) to = &panes[0];
            else if (gf.slot > 0 && gf.slot < 3 && panes[static_cast<size_t>(gf.slot)].empty()) to = &panes[static_cast<size_t>(gf.slot)];
        } else if (gf.enemy) {
            to = &panes[0];
        } else if (gf.slot == 0 || gf.slot == 2) {
            to = &panes[2];
        } else if (gf.slot == 1 && panes[1].empty()) {
            to = &panes[1];
        }
        if (to) to->push_back(&gf);
        else later.push_back(&gf);
    }
    for (const GalacticFleet* gf : later)
        for (auto& pane : panes)
            if (pane.empty()) {
                pane.push_back(gf);
                break;
            }
}

// The slot a drop on an empty organiser pane addresses: the selected planet's slot of that pane (legacy docs/re/UI.md §7.2, pane k = slot k).
GalacticHud::StackHit GalacticHud::pane_hit(const PadSlot& pad) const {
    StackHit h;
    h.component = pad.component;
    h.planet = selected_id_;
    h.slot = pad.fleet_index;
    return h;
}

const GalacticHud::PadSlot* GalacticHud::pad_of(int32_t component) const {
    if (component < 0) return nullptr;
    for (const PadSlot& s : pads_)
        if (s.component == component) return &s;
    return nullptr;
}

bool GalacticHud::pad_subject(int32_t component, PadSubject& out) const {
    const PadSlot* p = pad_of(component);
    if (!p || p->empty) return false;
    out = {};
    if (p->is_queue) {
        out.type = p->queue.type_index;
        return true;
    }
    if (std::find(create_.begin(), create_.end(), p->component) != create_.end()) {
        out.type = p->option.type_index;
        return true;
    }
    if (p->unit == 0) return false;  // a fleet header: the map fleet summary, not a unit pop-up
    out.object = p->unit;
    return true;
}

const GalacticHud::StackHit* GalacticHud::stack_at(int32_t component, float px, float py) const {
    if (component < 0) return nullptr;
    const Vec2f c = canvas_.pixel_to_canvas(px + 0.5f, py + 0.5f);
    const StackHit* best = nullptr;
    for (const StackHit& h : stack_hits_)  // last drawn wins (drawn on top)
        if (h.component == component && c.x >= h.min.x && c.x <= h.max.x && c.y >= h.min.y && c.y <= h.max.y) best = &h;
    return best;
}

uint32_t GalacticHud::stack_under(float px, float py, bool& land) const {
    land = false;
    const Vec2f c = canvas_.pixel_to_canvas(px + 0.5f, py + 0.5f);
    const StackHit* best = nullptr;
    for (const StackHit& h : stack_hits_)  // last drawn wins (drawn on top)
        if ((h.component == planet_fleet_ || h.component == planet_land_) && c.x >= h.min.x && c.x <= h.max.x && c.y >= h.min.y &&
            c.y <= h.max.y)
            best = &h;
    if (!best) return 0;
    land = best->component == planet_land_;
    return best->fleet;
}

bool GalacticHud::takes_button(int32_t component, Button b) const {
    if (b == Button::Right && component >= 0 && (component == planet_fleet_ || component == planet_land_)) return false;
    return true;
}

// #723 (docs/re/COMMAND_BAR_DRAG.md "Ghosting" [H]: the TextButton's drag start 0x352880): while a pad with Should_Ghost
// and Should_Render_At_Drag_Pos is dragged, a ghost of it stays in its place. Ghost_Base_Only: the ghost is the pad's
// base without the unit's icon or count. Otherwise a ghost only for a stack of two or more: the stack less the dragged
// unit, its count as "x%d" (0x804eb0) while above one. Without a ghost the pad draws as before. Base FoC sets
// Should_Render_At_Drag_Pos on no ghosting pad, so it never ghosts. #763: a whole-stack drag (+0x7dd) shows no ghost.
void GalacticHud::ghost_dragged_pad(int32_t component, int32_t count, ButtonRuntime& r) {
    if (!drag_.active || component < 0 || drag_.component != component) return;
    const CommandBarComponent& c = (*table_)[static_cast<size_t>(component)];
    const bool at_drag_pos = data::parse_flag_char(c.raw_value("SHOULD_RENDER_AT_DRAG_POS"));
    // #763 0x34f160 [H]: with Should_Render_At_Drag_Pos the dragged pad's own text is drawn at the drag position (or not
    // at all), never on the pad; the ghost, in its place, draws only while +0x7dd is clear.
    if (at_drag_pos) r.text.clear();
    if (drag_.whole_stack) return;
    if (!data::parse_flag_char(c.raw_value("SHOULD_GHOST")) || !at_drag_pos) return;
    if (data::parse_flag_char(c.raw_value("GHOST_BASE_ONLY"))) {
        r.icon_override.clear();
        r.text.clear();
    } else if (count >= 2) {
        r.text = count - 1 > 1 ? U"x" + num32(static_cast<uint32_t>(count - 1)) : std::u32string();
    } else {
        return;
    }
    ++stats_.ghosts;
}

// #722 (docs/re/COMMAND_BAR_DRAG.md "Snapping" [H]: the drag icon's move 0x352650, which the command bar's mouse move
// 0x2f6c30 calls with the component under the cursor): the icon sits at the cursor, or on the component under it when the
// dragged pad has Snap_Drag and that component has Snap_Location. Retail puts it on that component's bone; the port's
// placement centre is that bone's position. Base FoC: Pad_00..09 and every q1 / q2 / q3 pad have both.
Vec2f GalacticHud::drag_icon_position() const {
    const Vec2f cursor = canvas_.pixel_to_canvas(drag_.cursor_px.x, drag_.cursor_px.y);
    if (drag_.component < 0 || drag_.over_component < 0 || static_cast<size_t>(drag_.over_component) >= table_->size()) return cursor;
    const CommandBarComponent& from = (*table_)[static_cast<size_t>(drag_.component)];
    const CommandBarComponent& over = (*table_)[static_cast<size_t>(drag_.over_component)];
    if (!data::parse_flag_char(from.raw_value("SNAP_DRAG")) || !data::parse_flag_char(over.raw_value("SNAP_LOCATION"))) return cursor;
    const ComponentPlacement& p = layout_.placement(static_cast<uint32_t>(drag_.over_component));
    return p.placed ? p.center : cursor;
}

bool GalacticHud::draggable(int32_t component) const {
    const PadSlot* p = pad_of(component);
    if (p) return p->draggable;
    for (const StackHit& h : stack_hits_)  // a map fleet button: any draggable instance (the press picks the one)
        if (h.component == component && h.draggable) return true;
    return false;
}

int32_t GalacticHud::planet_at(float px, float py) const { return galactic_planet_at(planet_px_, px, py); }

const GalacticFleetSlot* galactic_fleet_lead(std::span<const GalacticFleetSlot> units) {
    for (const GalacticFleetSlot& u : units)
        if (u.lead && u.object != 0) return &u;
    return nullptr;
}

void galactic_unit_stacks(std::span<const GalacticFleetSlot> units, std::vector<GalacticUnitStack>& out) {
    galactic_unit_stacks(units, nullptr, out);
}

void galactic_unit_stacks(std::span<const GalacticFleetSlot> units, const GalacticFleetSlot* lead, std::vector<GalacticUnitStack>& out) {
    const size_t first = out.size();
    for (const GalacticFleetSlot& u : units) {
        if (&u == lead) continue;
        GalacticUnitStack* into = nullptr;
        if (u.type != kNoSlotType && u.name.empty())  // [port] a named unit keeps a pad of its own
            for (size_t k = first; k < out.size(); ++k)
                if (out[k].slot.type == u.type && out[k].slot.name.empty()) into = &out[k];
        if (into) {
            into->slot.count += u.count;
            into->slot.selected = into->slot.selected || u.selected;
            into->members.push_back(u.object);
        } else {
            out.push_back({u, {u.object}});
        }
    }
}

void galactic_planet_screens(const GalacticHudFrame& f, std::vector<GalacticPlanetScreen>& out) {
    out.clear();
    for (size_t i = 0; i < f.planets.size(); ++i) {
        const GalacticPlanetInfo& p = f.planets[i];
        if (!p.visible) continue;
        float sx = 0, sy = 0, rx = 0, ry = 0;
        if (!project(f.view, p.position, sx, sy)) continue;
        const Vec3 edge{p.position.x + f.view.right.x * p.radius, p.position.y + f.view.right.y * p.radius,
                        p.position.z + f.view.right.z * p.radius};
        const float r = project(f.view, edge, rx, ry) ? std::hypot(rx - sx, ry - sy) : 0.0f;
        out.push_back({p.planet, i, sx, sy, r});
    }
}

int32_t galactic_planet_at(std::span<const GalacticPlanetScreen> planets, float px, float py) {
    int32_t best = -1;
    float best_d = 1e30f;
    for (size_t i = 0; i < planets.size(); ++i) {
        const GalacticPlanetScreen& p = planets[i];
        const float d = std::hypot(p.x - px, p.y - py);
        if (d <= std::max(p.r * 1.2f, 24.0f) && d < best_d) {
            best_d = d;
            best = static_cast<int32_t>(i);
        }
    }
    return best;
}

bool galactic_organiser_drop(const GalacticOrganiserDrag& d, const GalacticDropTarget& t, uint32_t selected,
                             std::vector<commands::EventData>* out) {
    constexpr uint32_t kNone = commands::kNoObject;
    const auto emit = [out](commands::EventData e) {
        if (out) out->push_back(std::move(e));
        return true;
    };
    const uint32_t unit = d.unit != 0 ? d.unit : kNone;
    // #763: a Ctrl drop of a Stackable pad sends the row's command for every unit of the stack.
    const auto each_unit = [&](decltype(commands::kFleetMerge) op, uint32_t target, uint32_t fleet) {
        if (d.unit == 0 || d.stack.empty()) return emit(commands::fleet_management_event(op, target, fleet, unit));
        for (const uint32_t u : d.stack) emit(commands::fleet_management_event(op, target, fleet, u));
        return true;
    };
    switch (t.kind) {
        case GalacticDropTarget::Kind::Fleet:  // onto a fleet pad
            if (t.foreign) return false;       // a foreign pad is no drop target (legacy docs/re/UI.md §7.2)
            if (t.fleet == d.fleet) return false;
            if (d.from_fleet) return each_unit(commands::kFleetMerge, t.fleet, d.fleet);
            return each_unit(commands::kFleetLaunch, t.fleet, kNone);
        case GalacticDropTarget::Kind::LandForces:  // onto the land forces (a map fleet: only at the selected planet)
            if (d.from_fleet && selected != 0 && (d.planet == 0 || d.planet == selected))
                return each_unit(commands::kFleetLand, selected, d.fleet);
            return false;
        case GalacticDropTarget::Kind::World:
            if (!d.from_fleet) return false;  // a land unit into the world
            if (t.on_planet) {
                if (d.unit != 0) return emit(commands::ev::move_object_to_object(d.unit, t.planet));
                if (t.planet != (d.planet != 0 ? d.planet : selected)) return emit(commands::ev::move_object_to_object(d.fleet, t.planet));
                return false;
            }
            if (d.unit != 0 && selected != 0) return emit(fleet_management_event(commands::kFleetDetach, selected, kNone, d.unit));
            return false;
        case GalacticDropTarget::Kind::Slot: return galactic_slot_drop(d, t, selected, out);
        case GalacticDropTarget::Kind::Other: break;
    }
    return false;
}

void GalacticHud::drag_update(const RoutedInput& routed, std::vector<commands::EventData>& out, bool& dropped,
                              std::vector<HudAction>* actions) {
    const Vec2f cur{static_cast<float>(routed.world.mouse_x), static_cast<float>(routed.world.mouse_y)};
    for (const HudPointerEvent& e : routed.hud) {
        if (e.button != Button::Left) continue;
        if (e.kind == HudPointerEvent::Kind::Press) {
            press_component_ = draggable(e.component) ? e.component : -1;
            press_px_ = {static_cast<float>(e.x), static_cast<float>(e.y)};
            press_stack_ = {};
            if (press_component_ >= 0 && !pad_of(press_component_)) {
                const StackHit* sh = stack_at(press_component_, press_px_.x, press_px_.y);
                if (sh && sh->draggable) press_stack_ = *sh;
                else press_component_ = -1;
            }
        } else if (e.kind == HudPointerEvent::Kind::Release) {
            if (drag_.active) {
                drag_.cursor_px = {static_cast<float>(e.x), static_cast<float>(e.y)};
                drag_.over_component = e.component;
                const size_t sent = out.size();
                drop(&out, actions);
                dropped = true;
                // #767: every unit the drop moved pops in at its new pad (0x3cf7d0 and the organiser drops set record
                // +0x30 on each moved unit). Organiser and land-force drags only: a map pad drag is not traced.
                if (out.size() > sent && drag_.unit != 0 && drag_.fleet_index < kMapFleetIndex) {
                    const bool stack = ctrl_ && drag_.stackable;
                    for (const uint32_t u : stack && !drag_.stack.empty() ? drag_.stack : std::vector<uint32_t>{drag_.unit})
                        pops_.push_back({u, drag_.fleet, now_});
                }
            }
            press_component_ = -1;
            drag_ = {};
        }
    }
    if (press_component_ >= 0 && !drag_.active && routed.owner[static_cast<size_t>(Button::Left)] == PointerTarget::Hud &&
        std::hypot(cur.x - press_px_.x, cur.y - press_px_.y) > static_cast<float>(kDragThresholdPx)) {
        if (const PadSlot* p = pad_of(press_component_)) {
            drag_.active = true;
            drag_.component = p->component;
            drag_.fleet_index = p->fleet_index;
            drag_.fleet = p->fleet;
            drag_.unit = p->unit;
            drag_.no_lead = p->no_lead;
            drag_.icon = p->icon;
            // #763: the pad's units and its text (its count, as last drawn), and its Stackable flag (+0x3e1 bit 0x10).
            drag_.stack = p->select;
            if (const ButtonRuntime* br = buttons_.find_state(static_cast<uint32_t>(p->component))) drag_.text = br->text;
            const CommandBarComponent& pc = (*table_)[static_cast<size_t>(p->component)];
            drag_.stackable = data::parse_flag_char(pc.raw_value("STACKABLE"));
            drag_.can_drag_stack = data::parse_flag_char(pc.raw_value("CAN_DRAG_STACK"));  // +0x3e4 bit 0x02
        } else if (press_stack_.component == press_component_) {
            drag_.active = true;
            drag_.component = press_stack_.component;
            drag_.fleet_index = kMapFleetIndex;
            drag_.fleet = press_stack_.fleet;
            drag_.unit = 0;
            drag_.planet = press_stack_.planet;
            drag_.icon = press_stack_.icon;
            drag_.layers = press_stack_.layers;
            drag_.tint = press_stack_.tint;
        }
    }
    if (!drag_.active) return;
    drag_.cursor_px = cur;
    drag_.over_component = routed.hover_component;
    // #763 0x2f6c30 / 0x2f6bc0 [H]: +0x7dd = Ctrl held && the dragged pad's Can_Drag_Stack (+0x3e4 bit 0x02).
    drag_.whole_stack = ctrl_ && drag_.can_drag_stack;
    const PadSlot* over = pad_of(drag_.over_component);
    drag_.over_fleet = over && over->fleet_index >= 0 && !over->foreign && over->fleet != drag_.fleet;  // never merge into a foreign fleet
    if (GalacticDropTarget t; !over && drag_.over_component >= 0 && drop_targets_ && drop_targets_(drag_.over_component, t))
        drag_.over_fleet = t.kind == GalacticDropTarget::Kind::Fleet && !t.foreign && t.fleet != drag_.fleet;  // #613
    // #623 over_planet indexes the frame's planets (GalacticPlanetScreen::index), not planet_px_ (the planets on screen).
    const int32_t pi = drag_.over_component < 0 ? planet_at(cur.x, cur.y) : -1;
    drag_.over_planet = pi >= 0 ? static_cast<int32_t>(planet_px_[static_cast<size_t>(pi)].index) : -1;
    // Over a fleet slot (an empty oval or a fleet button): its planet counts as the planet under the cursor.
    drag_.over_slot = -1;
    drag_.over_slot_planet = drag_.over_slot_fleet = 0;
    if (over && over->empty && over->fleet == 0 && !over->foreign) {  // an empty pane: the drop makes a new fleet in its slot
        drag_.valid = drop(nullptr);
        drag_.over_fleet = drag_.valid;
        return;
    }
    if (drag_.over_component >= 0 && drag_.over_component == planet_fleet_)
        if (const StackHit* sh = stack_at(planet_fleet_, cur.x, cur.y)) {
            drag_.over_slot = sh->slot;
            drag_.over_slot_planet = sh->planet;
            drag_.over_slot_fleet = sh->fleet;
            for (const GalacticPlanetScreen& s : planet_px_)
                if (s.planet == sh->planet) drag_.over_planet = static_cast<int32_t>(s.index);
            drag_.valid = drop(nullptr);
            return;
        }
    drag_.valid = drop(nullptr);
}

bool GalacticHud::drop(std::vector<commands::EventData>* out, std::vector<HudAction>* actions) {
    const auto emit = [out](commands::EventData e) {
        if (out) out->push_back(std::move(e));
        return true;
    };
    if (drag_.fleet_index >= kMapOrbitIndex) return map_drop(out);  // a planet-map drag
    const PadSlot* over = pad_of(drag_.over_component);
    const bool from_fleet = drag_.fleet_index >= 0;
    // #613 the rules the Modern organiser shares (galactic_organiser_drop). #763 0x3d0790 [H]: with Ctrl held and the
    // dragged pad Stackable, the whole stack moves (Ctrl as it is now, +0x21a9); otherwise the one unit.
    GalacticOrganiserDrag source{from_fleet, drag_.fleet, drag_.unit, drag_.planet, {}};
    if (ctrl_ && drag_.stackable && drag_.unit != 0) source.stack = drag_.stack;
    using Target = GalacticDropTarget;
    if (over && over->fleet_index >= 0) {  // onto a fleet pad
        if (over->lead_pad) return lead_drop(over->component, out);  // #515
        // an empty pane k: a new fleet in slot k (legacy docs/re/UI.md §7.2, §7.3)
        if (!over->foreign && over->empty && over->fleet == 0) return selected_id_ != 0 && slot_drop(pane_hit(*over), out);
        return galactic_organiser_drop(source, Target{Target::Kind::Fleet, over->fleet, over->foreign}, selected_id_, out);
    }
    if (over) return galactic_organiser_drop(source, Target{Target::Kind::LandForces}, selected_id_, out);  // the land forces
    // #613 another view's organiser row under the cursor (set_drop_targets: the Modern organiser).
    if (Target t; drag_.over_component >= 0 && drop_targets_ && drop_targets_(drag_.over_component, t))
        return galactic_organiser_drop(source, t, selected_id_, out);
    if (drag_.over_component >= 0 && drag_.over_component == planet_fleet_) {  // onto a fleet slot
        const StackHit* sh = stack_at(planet_fleet_, drag_.cursor_px.x, drag_.cursor_px.y);
        return sh && slot_drop(*sh, out);
    }
    if (drag_.over_component >= 0 && drag_.over_component == special_) {  // #455: onto a g_special_ability row
        if (drag_.fleet_index != kMapFleetIndex) return false;
        const Vec2f c = canvas_.pixel_to_canvas(drag_.cursor_px.x + 0.5f, drag_.cursor_px.y + 0.5f);
        for (const AbilityRow& r : ability_rows_) {
            if (c.x < r.min.x || c.x > r.max.x || c.y < r.min.y || c.y > r.max.y) continue;
            // 0x2f6c30 -> 0x3d2900 [H for the branches]: at another planet the drop makes the planet drop's move [M]; at the
            // fleet's own planet it uses the ability there (the host's SpecialAbility activate).
            if (r.planet != drag_.planet) return emit(commands::ev::move_object_to_object(drag_.fleet, r.planet));
            if (out && actions) {
                HudAction a;
                a.kind = HudAction::Kind::Button;
                a.component = special_;
                a.name = (*table_)[static_cast<size_t>(special_)].name;
                a.unit = drag_.fleet;
                a.ids = {r.planet};
                a.button = Button::Left;
                actions->push_back(std::move(a));
            }
            return true;
        }
        return false;
    }
    if (drag_.over_component >= 0 || !from_fleet) return false;  // another HUD component / land unit into the world
    const int32_t pi = planet_at(drag_.cursor_px.x, drag_.cursor_px.y);
    Target world{Target::Kind::World};
    world.on_planet = pi >= 0;
    if (pi >= 0) world.planet = planet_px_[static_cast<size_t>(pi)].planet;
    return galactic_organiser_drop(source, world, selected_id_, out);
}

// #515 (legacy docs/re/UI.md §7.4, header-pad drop 0x3d63f0): a unit dragged from an own pane onto an own pane's header pad
// becomes that fleet's lead. From another pane it first merges into the pad's fleet (0x3d0790); then every lead of the
// pad's pane is cleared (0x3ccdd0: GUI index 999 -> -1) and the unit gets index 999 (SetGUIIndex). Refused: a unit whose
// type has the always-deployed property (retail: a HUD message and sound; [port] no message: presentation only, the drop
// is refused either way, tier 3), a whole fleet, a land or map drag, the pad's own lead, and [port] a pane without a fleet
// (retail's empty header pad is not traced as a target; there is no fleet to lead, so no state changes).
bool GalacticHud::lead_drop(int32_t over_component, std::vector<commands::EventData>* out) const {
    const PadSlot* over = pad_of(over_component);
    if (!over || !over->lead_pad || over->foreign || over->fleet == 0 || over->fleet_index < 0 || over->fleet_index >= 3) return false;
    if (drag_.fleet_index < 0 || drag_.fleet_index >= 3 || drag_.unit == 0 || drag_.no_lead || drag_.unit == over->unit) return false;
    if (!out) return true;
    galactic_lead_events(drag_.fleet, drag_.unit, over->fleet, drag_.fleet_index != over->fleet_index,
                         pane_leads_[static_cast<size_t>(over->fleet_index)], *out);
    return true;
}

void galactic_lead_events(uint32_t from, uint32_t unit, uint32_t to, bool merge, std::span<const uint32_t> leads,
                          std::vector<commands::EventData>& out) {
    if (merge) out.push_back(fleet_management_event(commands::kFleetMerge, to, from, unit));
    for (const uint32_t u : leads)
        if (u != unit) out.push_back(commands::ev::set_gui_index(u, commands::kNoGuiIndex));
    out.push_back(commands::ev::set_gui_index(unit, commands::kPathfinderGuiIndex));
}

void GalacticHud::build_objectives(const GalacticHudFrame& f) {
    if (!f.objectives_open || !f.providers.objectives || obj_back_ < 0) return;
    std::vector<HudObjective> objs;
    f.providers.objectives(objs);
    if (objs.empty()) return;
    const CommandBarComponent& back = (*table_)[static_cast<size_t>(obj_back_)];
    const Vec2f off = canvas_.wider_than_4_3() && back.has_default_offset_widescreen ? back.default_offset_widescreen
                                                                                       : back.default_offset;
    // Top-left of the panel: canvas centre + offset (+Y down) [port], then clamped inside the canvas.
    Vec2f tl{off.x - 0.5f * back.size.x, -off.y};
    tl.x = std::clamp(tl.x, -0.5f * canvas_.width, 0.5f * canvas_.width - back.size.x);
    tl.y = std::clamp(tl.y, -0.5f * canvas_.height + 40.0f, 0.5f * canvas_.height);
    const float width = back.has_size ? back.size.x : 266.0f;
    const CommandBarComponent* hdr = obj_header_ >= 0 ? &(*table_)[static_cast<size_t>(obj_header_)] : nullptr;
    const CommandBarComponent* txt = obj_text_ >= 0 ? &(*table_)[static_cast<size_t>(obj_text_)] : nullptr;
    const CommandBarComponent* ico = obj_icon_ >= 0 ? &(*table_)[static_cast<size_t>(obj_icon_)] : nullptr;
    const float header_spacing = hdr && hdr->has_size ? hdr->size.y : 18.0f;
    const float line = txt && txt->has_size ? txt->size.y : 16.0f;
    const float pad_x = back.offset.x, pad_y = back.offset.y;
    float y = tl.y - pad_y;
    std::u32string header = f.objectives_header;
    // #45 t08: the objectives panel header TEXT_TEST_OBJECTIVE_HEADER (panel 0x2fdef0,
    // legacy docs/re/MCX8_BINDINGS_PLAYERS_STORY.md; the tactical HUD's header) [M for the galactic panel];
    // TEXT_OBJECTIVES, which the port used before, is not in the FoC MasterTextFile.
    if (header.empty()) header = text_or("TEXT_TEST_OBJECTIVE_HEADER", U"Mission Objectives");  // retail's English
    const float y_top = tl.y;
    // Lines first (to size the background).
    struct Line {
        std::u32string text;
        float y;
        HudObjective::State state;
        bool first;
        bool marks;  // the completed / failed icon (not on a battle-info line)
    };
    std::vector<Line> lines;
    struct Header {
        std::u32string text;
        float y;
    };
    std::vector<Header> headers;
    const int32_t tf = font_of(obj_text_);
    // #691 the panel 0x2fdef0's sections [H]: the battle-info lines (Add_Objective(text, true)) under
    // TEXT_TEST_BATTLE_INFO_HEADER first while there are any, then the TEXT_TEST_OBJECTIVE_HEADER section (always).
    const auto section = [&](std::u32string title, bool battle_info) {
        headers.push_back({std::move(title), y - 0.5f * header_spacing});
        y -= header_spacing;
        for (const HudObjective& o : objs) {
            if (o.battle_info != battle_info) continue;
            const std::u32string t = hud_objective_text(text_, o);
            const float wrap_px = (width - 24.0f) / canvas_.units_per_pixel_x;
            std::vector<std::u32string> wrapped = tf >= 0 ? wrap_text(*fonts_, tf, t, wrap_px) : std::vector<std::u32string>{t};
            bool first = true;
            for (std::u32string& w : wrapped) {
                lines.push_back({std::move(w), y - 0.5f * line, o.state, first, !battle_info});
                first = false;
                y -= line;
            }
            ++stats_.objectives;
            if (battle_info) ++stats_.battle_info;
        }
    };
    if (std::any_of(objs.begin(), objs.end(), [](const HudObjective& o) { return o.battle_info; }))
        section(text_or("TEXT_TEST_BATTLE_INFO_HEADER", U"Battle Info"), true);  // [port] English stand-in
    section(std::move(header), false);
    const float bottom = y - pad_y;
    // Background (Blank_Texture_Name stretched, Color alpha) on Base_Layer.
    const uint32_t bl = static_cast<uint32_t>(std::clamp(back.base_layer, 0, 12));
    if (const std::string* t = pick(back.blank_texture, 0)) {
        const Vec2f c{tl.x + 0.5f * width, 0.5f * (y_top + bottom)};
        add_atlas_quad(dl_, bl, HudList::Alpha, *atlas_, *t, c, {width, y_top - bottom}, 1.0f, back.has_color ? back.color : Rgba8{}, {width, y_top - bottom});
    }
    const uint32_t tl_layer = static_cast<uint32_t>(std::clamp(hdr ? hdr->base_layer : 4, 0, 12));
    for (const Header& h : headers) text_at(obj_header_, h.text, {tl.x + pad_x, h.y}, tl_layer);
    for (const Line& l : lines) {
        const bool done = l.state == HudObjective::State::Complete, failed = l.state == HudObjective::State::Failed;
        Rgba8 col = txt && txt->has_text_color ? txt->text_color : Rgba8{};
        if (done && txt && txt->has_text_color2) col = txt->text_color2;
        if (failed && hdr && hdr->has_text_color2) col = hdr->text_color2;
        if (l.marks && l.first && ico && (done || failed)) {
            const std::string* t = done ? pick(ico->icon_texture, 0) : pick(ico->icon_alternate_texture, 0);
            if (t)
                add_atlas_quad(dl_, static_cast<uint32_t>(std::clamp(ico->base_layer, 0, 12)), HudList::Alpha, *atlas_, *t,
                               {tl.x + pad_x + 8.0f, l.y}, ico->has_size ? ico->size : Vec2f{16, 16}, 1.0f, Rgba8{},
                               {12, 12});
        }
        text_at(obj_text_, l.text, {tl.x + pad_x + 20.0f, l.y}, tl_layer, 255, &col);
    }
}

void GalacticHud::build_special_abilities(const GalacticHudFrame& f) {
    // #455 (docs/re/GALACTIC_ABILITY_ROWS.md "The row", 0x3eb200 refreshed for every planet by 0x3ea1b0 [H]): while an own
    // map fleet is dragged, each visible planet shows the one row the host classifies for it (only the first of the two
    // clones 0x3d3790 makes is ever shown [H]).
    if (special_ < 0 || !drag_.active || drag_.fleet_index != kMapFleetIndex || drag_.fleet == 0 || !f.providers.drag_special_ability)
        return;
    const auto alpha = static_cast<uint8_t>(std::clamp(std::lround(planet_label_alpha(f.zoom_state, f.zoom)), 0L, 255L));
    if (alpha == 0) return;
    const auto u = static_cast<uint32_t>(special_);
    const CommandBarComponent& c = (*table_)[u];
    const ComponentPlacement& pl = layout_.placement(u);
    const uint32_t layer = static_cast<uint32_t>(std::clamp(c.base_layer, 0, static_cast<int32_t>(kHudLayers) - 1));
    const Vec2f size = component_size(u, pl);
    const Vec2f cur = canvas_.pixel_to_canvas(f.cursor_px.x + 0.5f, f.cursor_px.y + 0.5f);
    for (const GalacticPlanetInfo& p : f.planets) {
        if (!p.visible) continue;
        GalacticSpecialAbility r;
        if (!f.providers.drag_special_ability(drag_.fleet, p.planet, r)) continue;
        float sx = 0, sy = 0;
        if (!project(f.view, p.position, sx, sy)) continue;
        const Vec2f at = canvas_.pixel_to_canvas(sx, sy);
        const Vec2f ctr{at.x + c.offset.x, at.y + c.offset.y};
        const Vec2f mn{ctr.x - 0.5f * size.x, ctr.y - 0.5f * size.y}, mx{ctr.x + 0.5f * size.x, ctr.y + 0.5f * size.y};
        const bool over = cur.x >= mn.x && cur.x <= mx.x && cur.y >= mn.y && cur.y <= mx.y;
        const Rgba8 tint = with_alpha(Rgba8{}, alpha);
        if (const std::string* frame = pick(c.blank_texture, 0))
            add_atlas_quad(dl_, layer, HudList::Alpha, *atlas_, *frame, ctr, {}, c.scale, tint, size);
        const std::string* icon = !r.icon.empty() ? &r.icon : pick(c.icon_alternate_texture, f.faction_alt);
        if (icon) add_atlas_quad(dl_, layer, HudList::Alpha, *atlas_, *icon, ctr, {}, c.scale, tint);
        if (over)
            if (const std::string* mo = pick(c.mouse_over_texture, f.faction_alt))
                add_atlas_quad(dl_, layer, HudList::Alpha, *atlas_, *mo, ctr, {}, c.scale, tint);
        std::u32string text = r.text;
        if (text.empty() && !r.text_key.empty())
            text = text_ && text_->find(r.text_key) ? gui_unescape(utf16_to_utf32(*text_->find(r.text_key))) : utf8_to_utf32(r.text_key);
        if (!text.empty()) {
            const Rgba8 col = c.has_text_color ? c.text_color : Rgba8{};
            text_at(special_, text, {ctr.x + c.text_offset.x, ctr.y - c.text_offset.y}, layer, alpha, &col);
        }
        hit(special_, layer, ctr, size);
        ability_rows_.push_back({mn, mx, p.planet});
        ++stats_.ability_rows;
    }
}

// The galactic minimap over radar_galactic: one g_radar_blip per visible planet in its owner colour (Selected_Texture for
// the selected planet) and the camera's footprint on the galaxy plane as a g_radar_view outline (i_radar_view is in no
// atlas: 1-unit lines in the same layer). #575: the mapping is retail's fixed +-380 x +-460 extent (galactic_radar_fit,
// 0x3d1260 [H]; +X right, +Y up). A planet outside that extent gets no blip (GalacticRadarFit::shows, [port] there).
void GalacticHud::build_radar(const GalacticHudFrame& f) {
    // ui4 t04 (GALACTIC_PLANET_MAP §8): while the planet map is open the blips are laid out on radar_galactic_adv_map.
    const int32_t radar = map_open_ && radar_adv_ >= 0 ? radar_adv_ : radar_;
    if (radar < 0) return;
    const ComponentPlacement& rp = layout_.placement(static_cast<uint32_t>(radar));
    if (!rp.placed || (map_open_ && !rp.from_bone)) return;
    const CommandBarComponent& rc = (*table_)[static_cast<size_t>(radar)];
    const Vec2f size = rp.size.x > 0 ? rp.size : rc.has_size ? rc.size : Vec2f{175, 175};
    const Vec2f rc_at = rp.size.x > 0 ? rp.rect_center : rp.center;
    // Opaque backdrop (playtest: trade routes showed through the radar box). In i_galactic_controls.alo the radar_galactic
    // bone's own mesh is visible but MeshAdditive (i_scan_lines.tga) - it cannot hide the world; what makes the box opaque
    // in the original is the faction faceplate (Empire/Rebel/Underworld_Faceplate_ALT<n>, MeshAlpha,
    // i_galactic_dashboard_wide*.tga), whose texels over the whole 175 x 175 radar rectangle have alpha 255 (checked on
    // the vanilla DDS). The HUD draws an opaque (PrimOpaque list) rectangle of the radar size on layer 0, i.e. before the
    // shell models (legacy docs/re/UI.md §1.1 step 5i < 6): with the faceplate drawn it is covered and invisible; without it (shells not
    // loaded / not drawn) it still stops the world from showing through. Blips (layer 3) and the view rectangle (layer 4)
    // draw over both. [port] colour = the faceplate's dark radar ground.
    add_atlas_quad(dl_, 0, HudList::Opaque, *atlas_, {}, rc_at, {}, 1.0f, kGalacticRadarBackdrop, size);
    stats_.radar_backdrop = true;
    radar_fit_ = {};
    hit(radar, static_cast<uint32_t>(std::max(0, rc.base_layer)), rc_at, size);  // the radar takes clicks
    if (f.planets.empty()) return;
    const GalacticRadarFit fit = galactic_radar_fit(f.planets);  // #613 shared with the Modern map
    radar_at_ = rc_at;
    radar_size_ = size;
    radar_fit_ = fit;
    auto to_radar = [&](float wx, float wy) { return fit.to_canvas(rc_at, size, wx, wy); };
    const CommandBarComponent* bc = radar_blip_ >= 0 ? &(*table_)[static_cast<size_t>(radar_blip_)] : nullptr;
    const uint32_t blip_layer = bc ? static_cast<uint32_t>(bc->base_layer) : 3u;
    for (const GalacticPlanetInfo& p : f.planets) {
        if (!p.visible) continue;
        if (!fit.shows(p.position.x, p.position.y)) continue;  // #575: off the extent
        const Vec2f at = to_radar(p.position.x, p.position.y);
        const std::string* t = nullptr;
        if (bc) t = p.selected && !bc->selected_texture.empty() ? &bc->selected_texture[0] : pick(bc->icon_texture, 0);
        Rgba8 col = p.owner_color;
        col.a = 255;
        if (!t || !add_atlas_quad(dl_, blip_layer, HudList::Alpha, *atlas_, *t, at, {}, bc->scale, col))
            add_atlas_quad(dl_, blip_layer, HudList::Alpha, *atlas_, {}, at, {}, 1.0f, col, {6, 6});
        ++stats_.radar_blips;
    }
    // Camera footprint (bounding box of the four ground corners), clipped to the radar.
    Vec2f a{}, b{};
    if (!galactic_radar_view(fit, f.view, rc_at, size, a, b)) return;
    const CommandBarComponent* vc = radar_view_ >= 0 ? &(*table_)[static_cast<size_t>(radar_view_)] : nullptr;
    const uint32_t view_layer = vc ? static_cast<uint32_t>(vc->base_layer) : 4u;
    const Rgba8 line{255, 255, 255, 220};
    auto bar = [&](Vec2f mn, Vec2f mx) {
        add_atlas_quad(dl_, view_layer, HudList::Alpha, *atlas_, {}, {0.5f * (mn.x + mx.x), 0.5f * (mn.y + mx.y)}, {}, 1.0f,
                       line, {mx.x - mn.x, mx.y - mn.y});
    };
    bar({a.x, a.y}, {b.x, a.y + 1.0f});
    bar({a.x, b.y - 1.0f}, {b.x, b.y});
    bar({a.x, a.y}, {a.x + 1.0f, b.y});
    bar({b.x - 1.0f, a.y}, {b.x, b.y});
    stats_.radar_view = true;
}

// Day pips (HUD_GAPS §4.2): ten balance_pip clones from the fight_bar_g bone of i_main_commandbar. [port] without the
// shell bones: right of Text_Day_Counter's fallback position.
void GalacticHud::build_day_pips(const GalacticHudFrame& f) {
    if (balance_pip_ < 0 || f.cycle_fraction < 0.0f) return;
    // Canvas position of bone `name` (case-insensitive) of shell component `shell`.
    auto bone_at = [&](int32_t shell, std::string_view name, Vec2f& out) {
        if (shell < 0) return false;
        const auto sc = static_cast<uint32_t>(shell);
        const Vec2f off = layout_.shell_offset(sc);
        for (const ShellBone& b : layout_.shell_bones(sc))
            if (ascii_iequals(b.name, name)) {
                out = {b.x + off.x, b.y + off.y};
                return true;
            }
        return false;
    };
    Vec2f at{};
    GalacticHudStats::DayPipAnchor anchor = GalacticHudStats::DayPipAnchor::None;
    if (f.advanced_map_open && bone_at(map_shell_, "day_clock", at))
        anchor = GalacticHudStats::DayPipAnchor::DayClock;
    else if (bone_at(commandbar_, "fight_bar_g", at))
        anchor = GalacticHudStats::DayPipAnchor::FightBar;
    if (anchor == GalacticHudStats::DayPipAnchor::None) {
        if (day_ < 0) return;
        const ComponentPlacement& dp = layout_.placement(static_cast<uint32_t>(day_));
        if (!dp.placed) return;
        at = {dp.center.x + 80.0f, dp.center.y};
        anchor = GalacticHudStats::DayPipAnchor::Fallback;
    }
    // Pixel_Align: snap to the pixel centre.
    const Vec2f px = canvas_.canvas_to_pixel(at.x, at.y);
    at = canvas_.pixel_to_canvas(std::floor(px.x) + 0.5f, std::floor(px.y) + 0.5f);
    stats_.day_pip_anchor = anchor;
    stats_.day_pip_origin = at;
    const auto ci = static_cast<uint32_t>(balance_pip_);
    const CommandBarComponent& c = (*table_)[ci];
    float w = 6.0f;  // [port] when the art is missing
    if (const std::string* t = pick(c.icon_alternate_texture, 1))
        if (const AtlasEntry* e = atlas_->find(*t)) w = static_cast<float>(e->width) * c.scale;
    const int32_t k = std::clamp(static_cast<int32_t>(std::floor(10.0f * f.cycle_fraction)), 0, 9);
    const uint32_t layer = static_cast<uint32_t>(std::max(c.base_layer, kShellLayerMin));
    ButtonRuntime& r = buttons_.state(ci);
    r.hidden = false;
    for (int32_t i = 0; i < 10; ++i) {
        r.alt = i <= k ? 1 : 0;
        r.selected = i == k && f.day_pip_flash;
        buttons_.draw(dl_, *atlas_, fonts_, canvas_, ci, {at.x + w * static_cast<float>(i), at.y}, {}, f.faction_alt, f.time,
                      static_cast<int32_t>(layer) - c.base_layer);
        ++stats_.day_pips;
        if (i <= k) ++stats_.day_pips_lit;
    }
    r.selected = false;
}

std::u32string GalacticHud::text_or(const char* key, std::u32string_view fallback) const {
    return text_or_port(text_, key, fallback);  // #45 t08: the English stand-in is a port string (pseudo-loc)
}

std::u32string auto_resolve_text(const TextDatabase* text, bool space, bool win, int32_t magnitude, std::u32string_view faction,
                                 std::u32string_view planet, int64_t losses) {
    static const char* const kMag[3] = {"MAJOR", "REGULAR", "MINOR"};
    const int32_t m = std::clamp(magnitude, 0, 2);
    char key[48];
    std::snprintf(key, sizeof key, "TEXT_AR_%s_%s_%s", space ? "SPACE" : "LAND", win ? "WIN" : "LOSS", kMag[m]);
    // [port-text-begin: the English stand-in of the TEXT_AR_* line when the table lacks it]
    static const char32_t* const kMagText[3] = {U"major", U"", U"minor"};
    std::u32string fb = U"##0 ";
    if (*kMagText[m]) fb += std::u32string(kMagText[m]) + U" ";
    fb += space ? U"space " : U"land ";
    fb += win ? U"victory at ##1. " : U"defeat at ##1. ";
    fb += space ? U"Ships lost: ##2" : U"Units lost: ##2";
    // [port-text-end]
    std::u32string s = text_or_port(text, key, fb);
    s = replace_all(std::move(s), U"##0", faction);
    s = replace_all(std::move(s), U"##1", planet);
    return replace_all(std::move(s), U"##2", num32(losses));
}

std::u32string auto_resolve_hero_loss_text(const TextDatabase* text, std::u32string_view hero, std::u32string_view planet) {
    std::u32string s = text_or_port(text, "TEXT_AR_HERO_LOSS", U"##0 defeated at ##1");
    s = replace_all(std::move(s), U"##0", hero);
    return replace_all(std::move(s), U"##1", planet);
}

// GALACTIC_DIALOGS §1: the battle-pending banner. With its shell loaded the plate is the .alo and the components sit on
// its bones; without it [port] a flat stand-in plate at the same top-centre spot (as the tactical pause banner).
void GalacticHud::build_battle_prompt(const GalacticHudFrame& f) {
    const GalacticBattlePrompt& b = f.battle;
    if (!b.active) return;
    const bool single = b.single;
    const int32_t title_c = single ? single_text_ : choice_text_;
    constexpr uint32_t kLayer = 9;  // Base_Layer 9 of the prompt components
    const float hh = 0.5f * canvas_.height;
    const bool from_shell = title_c >= 0 && layout_.placement(static_cast<uint32_t>(title_c)).from_bone;
    if (!from_shell) {
        // Plate: frame mesh of i_attack_mode_choice.alo [-218..218 x -94..16] around choice_frame (0, 106) [port stand-in].
        const float top = hh - kAttackShellTop;
        const Vec2f c{0.0f, top + (single ? 85.0f - 30.0f : 106.0f - 39.0f)}, sz{436.0f, single ? 70.0f : 110.0f};
        frame_rect(dl_, kLayer - 1, c, sz, Rgba8{70, 64, 30, 235}, Rgba8{196, 176, 70, 255});
    }
    // Title: planet + TEXT_TACTICAL_{SPACE,LAND}_BATTLE_IS_PENDING in Battle_Pending_Message_Color.
    std::u32string title = b.title;
    if (title.empty())
        title = b.planet + (b.space ? text_or("TEXT_TACTICAL_SPACE_BATTLE_IS_PENDING", U": SPACE BATTLE IMMINENT!")
                                    : text_or("TEXT_TACTICAL_LAND_BATTLE_IS_PENDING", U": PLANETARY BATTLE IMMINENT!"));
    if (title_c >= 0) {
        const ComponentPlacement& p = layout_.placement(static_cast<uint32_t>(title_c));
        if (p.placed) text_at(title_c, title, p.center, kLayer, 255, &b.color);
    }
    // Buttons (TextButton, caption in the component's red embossed text, i_attack_mode_rollover on hover).
    auto button = [&](int32_t ci, const char* key, const char32_t* fallback) {
        if (ci < 0) return;
        const auto u = static_cast<uint32_t>(ci);
        const ComponentPlacement& p = layout_.placement(u);
        if (!p.placed) return;
        ButtonRuntime& r = buttons_.state(u);
        r.hidden = false;
        r.text = text_or(key, fallback);
        Vec2f size = p.size.x > 0 ? p.size : Vec2f{158.0f, 20.0f};  // hidden 158 x 20 button mesh / rollover art
        const Vec2f at = p.size.x > 0 ? p.rect_center : p.center;
        if (!from_shell) frame_rect(dl_, kLayer, at, {size.x + 6.0f, size.y + 6.0f}, Rgba8{30, 26, 14, 235}, Rgba8{150, 130, 60, 255});
        buttons_.draw(dl_, *atlas_, fonts_, canvas_, u, at, size, f.faction_alt, f.time,
                      static_cast<int32_t>(kLayer) - (*table_)[u].base_layer);
        hit(ci, kLayer, at, size);
    };
    if (single) {
        button(single_button_, "TEXT_BUTTON_BEGIN", U"Begin");
    } else {
        button(choice_left_, "TEXT_BUTTON_BEGIN", U"Begin");
        button(choice_right_, "TEXT_BUTTON_RESOLVE_BATTLE", U"Auto-Resolve");
        // Commander icons on their force stacks.
        auto graph = [&](int32_t ci, const GalacticBattleSide& side) {
            if (ci < 0) return;
            const auto u = static_cast<uint32_t>(ci);
            const ComponentPlacement& p = layout_.placement(u);
            if (!p.placed) return;
            const CommandBarComponent& c = (*table_)[u];
            int32_t layers = side.stack;
            if (layers < 0) layers = static_cast<int32_t>(std::min<uint32_t>(5, (side.units + 4) / 5));
            layers = std::clamp(layers, 0, 5);
            if (const std::string* t = pick(c.blank_texture, layers))
                add_atlas_quad(dl_, kLayer, HudList::Alpha, *atlas_, *t, p.center, {}, c.scale, Rgba8{255, 255, 255, 255});
            const float icon_scale = c.has_size && c.size.x > 0 ? c.size.x : 0.75f;
            if (!side.icon.empty())
                add_atlas_quad(dl_, kLayer, HudList::Alpha, *atlas_, side.icon, {p.center.x, p.center.y + 3.0f * static_cast<float>(layers)},
                               {}, icon_scale, Rgba8{255, 255, 255, 255});  // no stand-in square for a missing icon
            ++stats_.battle_graphs;
        };
        graph(graph_left_, b.left);
        graph(graph_right_, b.right);
    }
    stats_.battle_prompt = true;
    stats_.battle_prompt_shell = from_shell;
}

// Screen-text lines (§2.1 / §3.1) [port placement]: centred, stacked down from 140 units below the top edge. Layer
// kScreenTextLayer, the last HUD layer drawn before the GUI dialogs (hud_frame_steps: L9-10, dialogs, L11-12), so an open
// dialog (the in-game menu, Save / Load) covers the lines like the rest of the HUD; L11-12 stay for what follows the
// cursor (drag icons) [port].
void GalacticHud::build_screen_text(const GalacticHudFrame& f) {
    if (f.screen_text.empty() || !fonts_) return;
    HudTextStyle st;
    st.font = fonts_->font("EmpireAtWar-Medium", 9.0f, canvas_.font_height());
    if (st.font < 0) return;
    st.outline = true;
    st.align = TextAlign::Center;
    float y = 0.5f * canvas_.height - 140.0f;
    for (const GalacticScreenText& l : f.screen_text) {
        if (l.text.empty() || l.alpha == 0) continue;
        st.color = with_alpha(l.color, l.alpha);
        st.outline_color = with_alpha(Rgba8{0, 0, 0, 255}, l.alpha);
        add_text(dl_, kScreenTextLayer, HudList::Text, *fonts_, canvas_, l.text, {0.0f, y}, st);
        y -= 16.0f;
        ++stats_.screen_text_lines;
    }
}

void GalacticHud::build(const GalacticHudFrame& f) {
    dl_.clear();
    hits_.clear();
    pads_.clear();
    stack_hits_.clear();
    ability_rows_.clear();
    stats_ = {};
    if (!table_) return;
    // Selected planet: the frame's flag, kept by id across frames.
    selected_ = -1;
    for (size_t i = 0; i < f.planets.size(); ++i)
        if (f.planets[i].selected) selected_ = static_cast<int32_t>(i);
    selected_id_ = selected_ >= 0 ? f.planets[static_cast<size_t>(selected_)].planet : 0;
    // Fleet slots (base 40.jpg): shown with a fleet selection or while dragging a fleet / unit.
    slots_shown_ = f.show_fleet_slots || drag_.active;
    empty_slot_color_ = f.empty_slot_color;
    for (const GalacticPlanetInfo& p : f.planets)
        for (const GalacticPlanetStack& st : p.stacks)
            slots_shown_ = slots_shown_ || (st.selected && !st.land);
    stats_.fleet_slots_shown = slots_shown_;
    paused_ = f.paused;
    if (pause_ >= 0) buttons_.state(static_cast<uint32_t>(pause_)).selected = f.paused;
    // GALACTIC_DIALOGS §4: the organiser shows with a selected planet in the planet close-up (host: organize_open) while
    // the advanced planet map is closed. §1: one of the two battle-prompt shells while the prompt is up.
    // #593 the Modern view replaces the command bar and the organiser (set_modern_overlay), not the planet map.
    overlay_ = modern_overlay_ && !f.advanced_map_open;
    const bool organize = f.organize_open && !f.advanced_map_open && !overlay_;
    const bool prompt2 = f.battle.active && !f.battle.single, prompt1 = f.battle.active && f.battle.single;
    shell_vis_.clear();
    auto shell = [&](int32_t ci, bool on) {
        if (ci < 0) return;
        layout_.set_shell_active(static_cast<uint32_t>(ci), on);
        shell_vis_.push_back({static_cast<uint32_t>(ci), on});
    };
    shell(organize_, organize);
    shell(pending_shell_, prompt2);
    shell(story_pending_shell_, prompt1);
    // ui4 t04 (GALACTIC_PLANET_MAP §1.2 / §1.3): the planet map replaces the command bar (and the organiser, above).
    map_open_ = f.advanced_map_open;
    shell(commandbar_, !map_open_ && !overlay_);
    shell(map_shell_, map_open_);
    stats_.organize_shown = organize;
    // Prompt buttons not shown this frame take no clicks.
    for (const int32_t ci : {choice_left_, choice_right_, single_button_})
        if (ci >= 0) buttons_.state(static_cast<uint32_t>(ci)).hidden = true;
    layout_.update(canvas_, *table_);
    build_planet_map(f);  // before build_bar: the map's text states feed its generic component loop
    if (!map_open_) {     // §1.4: the galaxy is covered by pm_backdrop
        build_labels(f);
        build_special_abilities(f);
    }
    build_bar(f);
    if (!overlay_) {
        build_radar(f);
        build_day_pips(f);
        build_fleets(f);
    }
    build_objectives(f);
    build_battle_prompt(f);
    build_screen_text(f);
    // FIXPASS_GALACTIC §4.2 (capture C2): as Movie_tactical, the bone at the movie's bottom-left corner, 200 x 200.
    if (movie_ >= 0 && f.advisor_page >= 0 && f.advisor_page < static_cast<int32_t>(kHudPageCount)) {
        const auto mu = static_cast<uint32_t>(movie_);
        const ComponentPlacement& p = layout_.placement(mu);
        if (p.placed) {
            const CommandBarComponent& c = (*table_)[mu];
            const Vec2f sz = c.size.x > 0 && c.size.y > 0 ? c.size : Vec2f{200.0f, 200.0f};
            add_movie_quad(dl_, layer_of(c, p), p.center, {p.center.x + sz.x, p.center.y + sz.y},
                           static_cast<uint16_t>(f.advisor_page), f.advisor_alpha);
            stats_.advisor_movie = true;
        }
    }
    // Planet screen discs for drop picking.
    galactic_planet_screens(f, planet_px_);
    // The dragged pad's icon follows the cursor (Should_Render_At_Drag_Pos), topmost HUD layer.
    if (drag_.active && drag_.component >= 0) {
        const CommandBarComponent& c = (*table_)[static_cast<size_t>(drag_.component)];
        const Vec2f at = drag_icon_position();
        std::string icon = drag_.icon;
        // Drag_Back ("show the pancakes while dragging"): a map fleet's stack under its icon.
        const std::string_view db = c.raw_value("DRAG_BACK");
        if (drag_.fleet_index == kMapFleetIndex && data::parse_flag_char(db))
            if (const std::string* b = pick(c.blank_texture, drag_.layers)) {
                add_atlas_quad(dl_, kHudLayers - 1, HudList::Alpha, *atlas_, *b, at, {}, c.scale,
                               Rgba8{drag_.tint.r, drag_.tint.g, drag_.tint.b, 200});  // the stack keeps its owner tint
            }
        if (icon.empty())
            if (const std::string* a = pick(c.icon_alternate_texture, f.faction_alt)) icon = *a;
        if (!icon.empty())
            add_atlas_quad(dl_, kHudLayers - 1, HudList::Alpha, *atlas_, icon, at, {}, c.scale, Rgba8{255, 255, 255, 200}, {32, 32});
        // #763 (docs/re/COMMAND_BAR_DRAG.md "Ctrl-drag of a stack", 0x34f160 draw state 5 [H]): a Should_Render_At_Drag_Pos
        // pad draws its text (its count) at the drag position, plus its Text_Offset; a Stackable one only while +0x7dd is
        // set (Ctrl && Can_Drag_Stack). That is the look only: the drop moves the stack on Ctrl && Stackable alone. [port]
        // On the drag icon's layer, where retail draws it at the pad's layer + 3 (at most 6); one line (no Max_Text_Width).
        if (fonts_ && !drag_.text.empty() && data::parse_flag_char(c.raw_value("SHOULD_RENDER_AT_DRAG_POS")) &&
            (!drag_.stackable || drag_.whole_stack)) {
            add_text(dl_, kHudLayers - 1, HudList::Text, *fonts_, canvas_, drag_.text, {at.x + c.text_offset.x, at.y - c.text_offset.y},
                     button_text_style(c, *fonts_, canvas_));
            ++stats_.drag_texts;
        }
    }
    // #71 t06 (legacy docs/re/HUD_GAPS.md §2.1 [H]): no component tooltip while a HUD modal is up (HUD +0x26a0 > 0: the
    // battle-pending shells).
    if (f.battle.active) {
        tooltip_.clear();
    } else {
        tooltip_.update(buttons_, *table_, tooltip_cfg_, f.time);
        if (fonts_)
            stats_.tooltip = tooltip_.draw(dl_, *atlas_, *fonts_, canvas_, *table_, buttons_, text_, f.cursor_px, f.faction_alt,
                                           tooltip_cfg_);
    }
    stats_.hit_boxes = static_cast<uint32_t>(hits_.size());
}

GalacticRadarFit galactic_radar_fit(std::span<const GalacticPlanetInfo> planets) {
    GalacticRadarFit fit;
    if (planets.empty()) return fit;
    float z = 0.0f;
    for (const GalacticPlanetInfo& p : planets) z += p.position.z;
    fit.z = z / static_cast<float>(planets.size());
    fit.half_x = kGalacticRadarHalfX;
    fit.half_y = kGalacticRadarHalfY;
    return fit;
}

bool galactic_radar_view(const GalacticRadarFit& fit, const View& view, Vec2f at, Vec2f size, Vec2f& a, Vec2f& b) {
    const std::array<Vec2f, 4> fp = camera_footprint(view, fit.z);
    float fx0 = fp[0].x, fx1 = fx0, fy0 = fp[0].y, fy1 = fy0;
    for (const Vec2f& c : fp) {
        fx0 = std::min(fx0, c.x);
        fx1 = std::max(fx1, c.x);
        fy0 = std::min(fy0, c.y);
        fy1 = std::max(fy1, c.y);
    }
    a = fit.to_canvas(at, size, fx0, fy0);
    b = fit.to_canvas(at, size, fx1, fy1);
    const Vec2f lo{at.x - 0.5f * size.x, at.y - 0.5f * size.y}, hi{at.x + 0.5f * size.x, at.y + 0.5f * size.y};
    a = {std::clamp(a.x, lo.x, hi.x), std::clamp(a.y, lo.y, hi.y)};
    b = {std::clamp(b.x, lo.x, hi.x), std::clamp(b.y, lo.y, hi.y)};
    return !(b.x - a.x < 1.0f || b.y - a.y < 1.0f);  // Classic's exact test (a NaN footprint is drawn as before)
}

// ui4 t07 (HUD_GAPS §10.1, 0x3d1260): a left click on the galactic radar moves the camera to that point; it never selects a
// planet. #575: the point is retail's fixed-extent mapping, the inverse of the blips'. [port] retail hands 0x448c20 the
// point with z = 10 (0x41200000); whether that is the camera's height or the look point's is not traced, and the host's
// RadarCameraJump keeps its camera height. Camera feel, tier 1: a known gap until 0x448c20 is read.
bool galactic_radar_click(const GalacticRadarFit& fit, Vec2f at, Vec2f size, Vec2f c, int32_t component, const std::string& name,
                          Button button, HudAction& out) {
    if (button != Button::Left || !fit.mapped() || size.x <= 0.0f || size.y <= 0.0f) return false;
    const Vec2f w = fit.to_world(at, size, c);
    out = {};
    out.kind = HudAction::Kind::RadarCameraJump;
    out.component = component;
    out.name = name;
    out.x = w.x;
    out.y = w.y;
    out.button = button;
    return true;
}

HudAction galactic_refused_action(int32_t component, const std::string& name, const GalacticBuildOption& o) {
    HudAction a;
    a.kind = HudAction::Kind::BuildRefused;
    a.component = component;
    a.name = name;
    a.value = static_cast<int32_t>(o.block);
    a.unit = o.type_index;
    return a;
}

void galactic_option_click(const GalacticBuildOption& o, Button button, uint32_t planet, std::vector<commands::EventData>& out) {
    if (planet == 0 || !galactic_option_buyable(o)) return;
    if (button == Button::Left) out.push_back(commands::ev::production(o.type_index, planet));
    else if (button == Button::Right) out.push_back(commands::ev::production(o.type_index, planet, true));  // [port] right click cancels
}

commands::EventData galactic_queue_cancel(const HudQueueEntry& q) {
    return commands::ev::production(q.type_index, q.location, true, q.queue, q.index);
}

HudAction galactic_select_action(int32_t component, std::vector<uint32_t> ids, Button button, bool shift, bool ctrl) {
    HudAction a;
    a.kind = HudAction::Kind::GalacticSelect;
    a.component = component;
    a.ids = std::move(ids);
    a.button = button;
    a.shift = shift;
    a.ctrl = ctrl;
    return a;
}

void GalacticHud::update_input(const RoutedInput& routed, bool shift, bool ctrl, double now,
                               std::vector<commands::EventData>& out, std::vector<HudAction>* actions) {
    if (!table_) return;
    ctrl_ = ctrl;  // #763 HUD +0x21a9
    now_ = now;
    bool dropped = false;
    drag_update(routed, out, dropped, actions);
    std::vector<ButtonClick> clicks;
    buttons_.update(routed.hover_component, routed.hud, shift, ctrl, now, clicks);
    if (dropped) clicks.clear();  // the release ended a drag, not a click
    auto act = [&](HudAction a) {
        if (actions) actions->push_back(std::move(a));
    };
    // a left click on a greyed build pad (HudButtons drops clicks on disabled components) is refused here with
    // the GUI negative feedback instead of a Production command the sim would refuse silently.
    if (!dropped)
        for (const HudPointerEvent& e : routed.hud) {
            if (e.kind != HudPointerEvent::Kind::Click || e.button != Button::Left || e.component < 0) continue;
            for (const PadSlot& s : pads_) {
                if (s.component != e.component || s.is_queue || s.foreign || !s.select.empty()) continue;
                if (galactic_option_buyable(s.option)) break;
                act(galactic_refused_action(e.component, (*table_)[static_cast<size_t>(e.component)].name, s.option));
                break;
            }
        }
    for (const ButtonClick& k : clicks) {
        bool handled = false;
        for (const PadSlot& s : pads_) {
            if (s.component != k.component) continue;
            handled = true;
            if (s.foreign) break;  // legacy docs/re/UI.md §7.2 [port]: a foreign fleet's pads are drawn, not clickable
            if (s.empty) break;    // an empty organiser pad (a drop target, #515's empty header pad included) has no click
            if (s.is_queue) {
                if (k.button == Button::Left) {
                    out.push_back(galactic_queue_cancel(s.queue));
                    buttons_.play_sfx("GUI_Cancel_SFX", k.component);  // CommandBarClass::Cancel_Production (legacy docs/re/UI.md §3.6)
                }
            } else if (!s.select.empty()) {
                act(galactic_select_action(k.component, s.select, k.button, k.shift, k.ctrl));
                // [port: Gabriel] a second left click on the same own pad within 0.5 s and 4 px (HeroPanel's double-click
                // rule) asks the host to rename the fleet (header) or the pad's unit.
                if (k.button == Button::Left && !k.shift && !k.ctrl) {
                    const bool dbl = last_pad_click_ == k.component && now - last_pad_click_time_ <= 0.5 &&
                                     std::abs(k.x - last_pad_click_x_) <= 4 && std::abs(k.y - last_pad_click_y_) <= 4;
                    if (dbl && (s.unit != 0 || s.fleet != 0)) {
                        HudAction r;
                        r.kind = HudAction::Kind::Rename;
                        r.component = k.component;
                        r.name = (*table_)[static_cast<size_t>(k.component)].name;
                        r.unit = s.unit != 0 ? s.unit : s.fleet;
                        r.value = s.unit != 0 ? 0 : 1;
                        if (s.fleet != 0) r.ids = {s.fleet};
                        act(std::move(r));
                        last_pad_click_ = -1;
                    } else {
                        last_pad_click_ = k.component;
                        last_pad_click_time_ = now;
                        last_pad_click_x_ = k.x;
                        last_pad_click_y_ = k.y;
                    }
                }
            } else {
                galactic_option_click(s.option, k.button, selected_id_, out);  // HudButtons clicks only enabled (buyable) pads
            }
            break;
        }
        if (handled) continue;
        if (k.component >= 0 && (k.component == planet_fleet_ || k.component == planet_land_)) {
            // GALAXY_MAP_PRESENTATION §1 map fleet / land-forces button: left click selects that fleet [port: right = none].
            const StackHit* sh = stack_at(k.component, static_cast<float>(k.x), static_cast<float>(k.y));
            if (sh && sh->fleet != 0 && k.button == Button::Left) {  // [inferred] an empty slot oval: no click action
                HudAction a;
                a.kind = HudAction::Kind::GalacticSelect;
                a.component = k.component;
                a.name = (*table_)[static_cast<size_t>(k.component)].name;
                a.ids = {sh->fleet};
                a.unit = sh->planet;
                a.value = sh->slot;
                a.button = k.button;
                a.shift = k.shift;
                a.ctrl = k.ctrl;
                act(std::move(a));
            }
            continue;
        }
        // ui4 t07 (HUD_GAPS §10.1): a left click on the galactic radar moves the camera there (galactic_radar_click).
        if (k.component >= 0 && (k.component == radar_ || k.component == radar_adv_)) {
            const Vec2f c = canvas_.pixel_to_canvas(static_cast<float>(k.x) + 0.5f, static_cast<float>(k.y) + 0.5f);
            HudAction a;
            if (galactic_radar_click(radar_fit_, radar_at_, radar_size_, c, k.component, (*table_)[static_cast<size_t>(k.component)].name,
                                     k.button, a))
                act(std::move(a));
            continue;
        }
        // ui4 t12 (GALACTIC_DIALOGS §5.1.1): the build tabs and the dial (which clicks the other tab's button).
        if (k.button == Button::Left && k.component >= 0 && (k.component == space_tab_ || k.component == planet_tab_ || k.component == dial_)) {
            const bool to_space = k.component == dial_ ? !build_space_tab_ : k.component == space_tab_;
            if (to_space || land_tab_allowed_) set_build_tab(to_space);
            continue;
        }
        if (k.component == pause_ && k.button == Button::Left) {  // [port: Gabriel] pause through the command stream
            out.push_back(pause_event(!paused_));
            continue;
        }
        HudAction a;
        a.component = k.component;
        a.name = (*table_)[static_cast<size_t>(k.component)].name;
        a.button = k.button;
        a.shift = k.shift;
        a.ctrl = k.ctrl;
        if (k.component == planet_info_) {
            a.kind = HudAction::Kind::PlanetInfo;
            a.unit = selected_id_;
        } else if (k.button == Button::Left && (k.component == choice_left_ || k.component == single_button_)) {
            a.kind = HudAction::Kind::BattleBegin;  // GALACTIC_DIALOGS §1.5
        } else if (k.button == Button::Left && k.component == choice_right_) {
            a.kind = HudAction::Kind::BattleAutoResolve;
        } else {
            a.kind = HudAction::Kind::Button;
        }
        act(std::move(a));
    }
}

}  // namespace alamo::ui
