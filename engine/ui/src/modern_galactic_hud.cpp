#include "alamo/ui/modern_galactic_hud.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>

#include "alamo/core/ascii.hpp"
#include "alamo/ui/hud_font.hpp"
#include "alamo/ui/hud_radar.hpp"
#include "alamo/ui/orders.hpp"
#include "modern_hud_draw.hpp"
#include "ui_strings.hpp"

namespace alamo::ui {

using namespace modern_draw;

namespace {

constexpr float kDeckHeight = 132.0f;  // canvas units: the tabs row over the option tiles
constexpr size_t kHeroRows = 11;       // the hero bar's entries at most
constexpr const char32_t* kTabLabels[kGalacticBuildTabs] = {U"Space", U"Ground", U"Buildings", U"Heroes"};

// "+1,250 / day" (a loss without the plus).
std::u32string per_day(const TextDatabase* text, int64_t v) {
    std::u32string n = hud_thousands(v);
    if (v > 0) n.insert(n.begin(), U'+');
    return replace_all(text_or_port(text, nullptr, U"##0 / day"), U"##0", n);
}

}  // namespace

GalacticBuildTab galactic_build_tab(const GalacticBuildOption& o) {
    if (o.hero) return GalacticBuildTab::Heroes;
    if (o.structure) return GalacticBuildTab::Buildings;
    return o.space ? GalacticBuildTab::Space : GalacticBuildTab::Ground;
}

void ModernGalacticHud::init(const ComponentTable& table, const TextureAtlas& atlas, FontCache& fonts, const TextDatabase* text) {
    init_view(table, atlas, fonts, text);
    option_ = table.find("b_option_g");
    pause_ = table.find("b_play_pause");
    fast_forward_ = table.find("b_fast_forward");
    holocron_ = table.find("b_story_arc_g");
    planet_info_ = table.find("planet_info");
    base_layout_ = table.find("advanced_planetary");
    radar_ = table.find("radar_galactic");  // #613
    if (const int32_t c = table.find("Text_Credits"); c >= 0 && table[static_cast<size_t>(c)].has_text_color)
        credits_color_ = table[static_cast<size_t>(c)].text_color;
}

void ModernGalacticHud::build(const GalacticHudFrame& f) {
    begin_build(f.faction_alt);
    stats_ = {};
    fleets_.clear();
    options_shown_.clear();
    queue_.clear();
    heroes_.clear();
    org_.clear();
    land_row_ = false;
    map_side_ = 0.0f;
    map_fit_ = {};
    for (bool& t : tab_live_) t = false;
    paused_ = f.paused;
    // The selected planet: the frame's flag (GalacticHud::build).
    selected_ = -1;
    for (size_t i = 0; i < f.planets.size(); ++i)
        if (f.planets[i].selected) selected_ = static_cast<int32_t>(i);
    selected_id_ = selected_ >= 0 ? f.planets[static_cast<size_t>(selected_)].planet : 0;
    galactic_planet_screens(f, planet_px_);  // #613 the organiser drag's planets in the world
    if (!table_ || !fonts_ || font() < 0 || f.advanced_map_open) {  // the planet map is Classic's
        drag_ = {};
        press_pad_ = -1;
        return;
    }
    build_map(f);
    build_deck(f);
    build_status(f);
    build_controls(f);
    // The battle prompt's banner takes the top centre (GalacticHud::build_battle_prompt).
    const ModernAlertStrip strip = alert_strip(f.battle.active ? std::span<const HudAlert>{} : f.alerts);
    stats_.alert_text = strip.text;
    stats_.alerts_more = strip.more;
    stats_.jump = strip.jump;
    build_heroes(f, status_bottom_ - kPad);
    build_drag();
    stats_.key_badges = key_badges_;
    stats_.hit_boxes = static_cast<uint32_t>(hits_.size());
    stats_.quads = static_cast<uint32_t>(dl_.total());
}

void ModernGalacticHud::build_status(const GalacticHudFrame& f) {
    struct Row {
        std::u32string label, value;
        Rgba8 color;
    };
    // The Classic top bar's values (GalacticHud::build_bar: Text_Credits, Text_Day_Credits, Text_Pop_Cap, Text_Tech_Level,
    // Text_Day_Counter) and the planets owned.
    stats_.credits_text = f.credits != kNoGalacticAmount ? hud_credits_text(f.credits) : std::u32string();
    stats_.income_text = f.income != kNoGalacticAmount ? per_day(text_, f.income) : std::u32string();
    stats_.pop_text = hud_pop_text(f.pop, f.pop_cap);
    stats_.tech_text = hud_tech_text(text_, f.tech);
    stats_.planets_text = f.planets_owned >= 0 ? count_text(static_cast<uint32_t>(f.planets_owned)) : std::u32string();
    stats_.day_text = f.day >= 0 ? count_text(static_cast<uint32_t>(f.day)) : std::u32string();
    std::vector<Row> rows;
    if (!stats_.credits_text.empty()) rows.push_back({{}, stats_.credits_text, credits_color_});
    if (!stats_.income_text.empty()) rows.push_back({text_or_port(text_, nullptr, U"Income"), stats_.income_text, colours_.text});
    if (!stats_.pop_text.empty()) rows.push_back({text_or_port(text_, nullptr, U"Population"), stats_.pop_text, colours_.text});
    if (!stats_.tech_text.empty()) rows.push_back({{}, stats_.tech_text, colours_.text});
    if (!stats_.planets_text.empty()) rows.push_back({text_or_port(text_, nullptr, U"Planets"), stats_.planets_text, colours_.text});
    if (!stats_.day_text.empty()) rows.push_back({text_or_port(text_, nullptr, U"Day"), stats_.day_text, colours_.text});
    const float hw = 0.5f * canvas_.width, hh = 0.5f * canvas_.height;
    status_bottom_ = hh - kMargin;
    if (rows.empty()) return;
    HudTextStyle value_style, label_style;
    value_style.font = label_style.font = font();
    label_style.color = colours_.label;
    const float line = text_height_units() * 1.5f;
    float label_w = 0.0f, value_w = 0.0f;
    for (const Row& r : rows) {
        if (!r.label.empty()) label_w = std::max(label_w, measure_text_block(*fonts_, canvas_, r.label, 0.0f, label_style).width + kPad);
        value_w = std::max(value_w, measure_text_block(*fonts_, canvas_, r.value, 0.0f, value_style).width);
    }
    const Vec2f size{label_w + value_w + 2.0f * kPad, line * static_cast<float>(rows.size()) + kPad};
    const Vec2f center{-hw + kMargin + 0.5f * size.x, hh - kMargin - 0.5f * size.y};
    frame_rect(dl_, kPanelLayer, center, size, colours_.back, accent(200));
    hit(kModernPanelHit, kPanelLayer, center, size);
    float y = center.y + 0.5f * size.y - 0.5f * kPad - 0.5f * line;
    const float left = center.x - 0.5f * size.x + kPad;
    for (const Row& r : rows) {  // labels in one column, the values after them
        if (!r.label.empty()) add_text(dl_, kPanelLayer, HudList::Text, *fonts_, canvas_, r.label, {left, y}, label_style);
        value_style.color = r.color;
        add_text(dl_, kPanelLayer, HudList::Text, *fonts_, canvas_, r.value, {left + label_w, y}, value_style);
        y -= line;
    }
    status_bottom_ = center.y - 0.5f * size.y;
    stats_.status_corner = true;
}

void ModernGalacticHud::build_controls(const GalacticHudFrame& f) {
    const float h = text_height_units() * 2.0f;
    const float y = 0.5f * canvas_.height - kMargin - 0.5f * h;
    float right = 0.5f * canvas_.width - kMargin;  // laid out right to left
    auto button = [&](int32_t id, std::u32string_view label, bool selected) {
        if (id < 0) return;
        right -= control_button(id, label, selected, right, y, h) + 0.5f * kPad;
        ++stats_.controls;
    };
    // The Classic components: menu, pause, fast forward and the holocron (lit while a chapter is unread).
    button(option_, text_or_port(text_, nullptr, U"Menu"), false);
    button(pause_, f.paused ? U">" : U"||", f.paused);
    button(fast_forward_, U">>", false);
    button(holocron_, text_or_port(text_, nullptr, U"Holocron"), f.holocron_flash);
    right -= kPad;
    style_switch(right, y, h);
    stats_.style_switch = true;
}

// The hero list: a row per roster entry (its icon, name and where it is: the planet, in transit, in production or
// returning), under the status corner.
void ModernGalacticHud::build_heroes(const GalacticHudFrame& f, float top) {
    if (f.heroes.empty()) return;
    std::vector<HeroEntry> sorted(f.heroes.begin(), f.heroes.end());
    sort_hero_entries(sorted);  // the bar's order
    const float th = text_height_units(), line = th * 1.5f;
    const size_t rows = std::min(sorted.size(), kHeroRows);
    const float width = std::min(0.24f * canvas_.width, 20.0f * th);
    const float left = -0.5f * canvas_.width + kMargin;
    const float header = line;
    const Vec2f size{width, header + line * static_cast<float>(rows) + kPad};
    const Vec2f center{left + 0.5f * width, top - 0.5f * size.y};
    frame_rect(dl_, kPanelLayer, center, size, colours_.back, accent(200));
    hit(kModernPanelHit, kPanelLayer, center, size);
    HudTextStyle hs, ns, ws;
    hs.font = ns.font = ws.font = font();
    hs.color = colours_.label;
    ns.color = colours_.text;
    ws.color = colours_.label;
    ws.align = TextAlign::Right;
    float y = top - 0.5f * kPad - 0.5f * header;
    add_text(dl_, kPanelLayer, HudList::Text, *fonts_, canvas_, text_or_port(text_, nullptr, U"Heroes"), {left + kPad, y}, hs);
    const std::u32string transit = text_or_port(text_, nullptr, U"In transit"), not_ready = text_or_port(text_, nullptr, U"Not ready");
    for (size_t i = 0; i < rows; ++i) {
        const HeroEntry& h = sorted[i];
        y -= line;
        const auto id = kModernGalacticHeroBase + static_cast<int32_t>(i);
        const Vec2f rc{center.x, y}, rs{width - kPad, line - 2.0f};
        if (hover_ == id) frame_rect(dl_, kControlLayer, rc, rs, colours_.button, colours_.text);
        float x = left + kPad;
        if (!h.icon.empty() && atlas_) {
            const float side = line - 4.0f;
            add_atlas_quad(dl_, kControlLayer, HudList::Alpha, *atlas_, h.icon, {x + 0.5f * side, y}, {side, side}, 1.0f, Rgba8{255, 255, 255, 255});
            x += side + 0.5f * kPad;
        }
        // Being built or returning (the bar's dial: respawn 0..1), else in hyperspace, else the planet's name.
        std::u32string where = transit;
        if (h.object == 0 || h.object == commands::kNoObject) {
            where = not_ready;
            if (h.respawn >= 0.0f)
                where += U" " + count_text(static_cast<uint32_t>(std::lround(100.0f * std::clamp(h.respawn, 0.0f, 1.0f)))) + U"%";
        }
        for (const GalacticPlanetInfo& p : f.planets)
            if (hero_focus_planet(h) != 0 && p.planet == h.planet) where = p.name;
        const std::u32string& name = h.tooltip.empty() ? ascii32(h.type_name) : h.tooltip;
        const float where_w = measure_text_block(*fonts_, canvas_, where, 0.0f, ws).width;
        const float right = left + width - kPad;
        ns.width_scale = fit_width_scale(measure_text_block(*fonts_, canvas_, name, 0.0f, ns).width, std::max(1.0f, right - where_w - kPad - x));
        add_text(dl_, kControlLayer, HudList::Text, *fonts_, canvas_, name, {x, y}, ns);
        add_text(dl_, kControlLayer, HudList::Text, *fonts_, canvas_, where, {right, y}, ws);
        hit(id, kControlLayer, rc, rs);
        heroes_.push_back(h);
        ++stats_.heroes;
    }
}

// With a planet selected: [planet card | fleets in orbit | build tabs and options | queue] along the bottom, beside the
// map; in the planet close-up the organiser above it (#613).
void ModernGalacticHud::build_deck(const GalacticHudFrame& f) {
    if (selected_ < 0) return;
    const GalacticPlanetInfo& p = f.planets[static_cast<size_t>(selected_)];
    const float beside = map_side_ > 0.0f ? map_side_ + kMargin : 0.0f;
    const float region_left = -0.5f * canvas_.width + kMargin + (options_.map_right ? 0.0f : beside);
    const float region_right = 0.5f * canvas_.width - kMargin - (options_.map_right ? beside : 0.0f);
    const float width = region_right - region_left;
    const Vec2f center{0.5f * (region_left + region_right), -0.5f * canvas_.height + kMargin + 0.5f * kDeckHeight};
    frame_rect(dl_, kPanelLayer, center, {width, kDeckHeight}, colours_.back, accent(200));
    hit(kModernPanelHit, kPanelLayer, center, {width, kDeckHeight});
    stats_.deck = true;
    if (f.organize_open) build_organiser(f, p, {region_left, center.y + 0.5f * kDeckHeight + kPad}, 0.6f * width);
    const float height = kDeckHeight - 2.0f * kPad;
    float left = center.x - 0.5f * width + kPad;
    float right = center.x + 0.5f * width - kPad;
    const float top = center.y + 0.5f * kDeckHeight - kPad;
    left += build_planet_card(f, p, {left, top}, height) + 2.0f * kPad;
    if (const float fw = build_fleets(f, p, {left, top}, height, 0.25f * (right - left)); fw > 0.0f) left += fw + 2.0f * kPad;
    if (const float qw = build_queue(f, {right, top}, height, 0.3f * (right - left)); qw > 0.0f) right -= qw + 2.0f * kPad;
    build_options(f, p, {left, top}, height, right - left);
}

float ModernGalacticHud::build_planet_card(const GalacticHudFrame& f, const GalacticPlanetInfo& p, Vec2f top_left, float height) {
    const float th = text_height_units(), line = th * 1.5f;
    const float width = 12.0f * th;
    HudTextStyle st;
    st.font = font();
    st.color = Rgba8{p.owner_color.r, p.owner_color.g, p.owner_color.b, 255};
    // The name in capitals and the owner's colour (Classic's Text_Planet), its income (Text_Planetary_Income).
    std::u32string name = p.name;
    for (char32_t& ch : name)
        if (ch >= U'a' && ch <= U'z') ch = static_cast<char32_t>(ch - U'a' + U'A');
    float y = top_left.y - 0.5f * line;
    st.width_scale = fit_width_scale(measure_text_block(*fonts_, canvas_, name, 0.0f, st).width, width);
    add_text(dl_, kPanelLayer, HudList::Text, *fonts_, canvas_, name, {top_left.x, y}, st);
    stats_.planet_name = name;
    if (p.income != kNoGalacticAmount) {
        y -= line;
        HudTextStyle is = st;
        is.width_scale = 1.0f;
        add_text(dl_, kPanelLayer, HudList::Text, *fonts_, canvas_, per_day(text_, p.income), {top_left.x, y}, is);
    }
    // Planet Info and Base Layout: the organiser's buttons, so only in the planet close-up (organize_open).
    if (f.organize_open) {
        const float bh = std::min(2.0f * th, 0.3f * height);
        float x = top_left.x;
        for (const auto& [id, label] : {std::pair{planet_info_, U"Planet Info"}, std::pair{base_layout_, U"Base Layout"}}) {
            if (id < 0) continue;
            Tile t;
            t.id = id;
            t.text = text_or_port(text_, nullptr, label);
            HudTextStyle ms;
            ms.font = font();
            const float w = std::min(0.5f * width - 0.25f * kPad, measure_text_block(*fonts_, canvas_, t.text, 0.0f, ms).width + 2.0f * kPad);
            t.size = {w, bh};
            t.c = {x + 0.5f * w, top_left.y - height + 0.5f * bh};
            tile(t);
            x += w + 0.5f * kPad;
            ++stats_.planet_buttons;
        }
    }
    stats_.planet_card = true;
    return width;
}

// The fleets in orbit (GalacticProviders::fleets, the organiser's feed): a tile each - the first unit's icon, the fleet's
// name, its unit count. Other players' fleets are drawn inert (the organiser's foreign panes).
float ModernGalacticHud::build_fleets(const GalacticHudFrame& f, const GalacticPlanetInfo& p, Vec2f top_left, float height, float max_width) {
    if (!f.providers.fleets) return 0.0f;
    f.providers.fleets(p.planet, fleets_);
    std::stable_partition(fleets_.begin(), fleets_.end(), [](const GalacticFleet& g) { return !g.enemy; });  // own first
    std::erase_if(fleets_, [](const GalacticFleet& g) { return g.fleet == 0 || g.units.empty(); });
    if (fleets_.empty()) return 0.0f;
    const float gap = 0.5f * kPad;
    const float side = 0.5f * (height - gap);
    const auto fit = static_cast<size_t>(std::max(1.0f, std::floor((max_width + gap) / (side + gap)) * 2.0f));
    if (fleets_.size() > fit) fleets_.resize(fit);
    const size_t cols = (fleets_.size() + 1) / 2;
    for (size_t i = 0; i < fleets_.size(); ++i) {
        const GalacticFleet& g = fleets_[i];
        Tile t;
        t.id = kModernGalacticFleetBase + static_cast<int32_t>(i);
        t.size = {side, side};
        t.c = {top_left.x + static_cast<float>(i / 2) * (side + gap) + 0.5f * side, top_left.y - static_cast<float>(i % 2) * (side + gap) - 0.5f * side};
        t.enabled = !g.enemy;
        t.icon = g.units.front().icon;
        t.text = !g.name.empty() ? g.name : text_or_port(text_, nullptr, g.enemy ? U"Enemy" : U"Fleet");
        uint32_t n = 0;
        for (const GalacticFleetSlot& u : g.units) n += u.count;
        t.badge = count_text(n);
        tile(t);
        ++stats_.fleets;
        if (g.enemy) ++stats_.enemy_fleets;
    }
    return static_cast<float>(cols) * side + static_cast<float>(cols - 1) * gap;
}

// The selected planet's build queue (GalacticProviders::build_queue, the Classic queue domes' feed): a tile per entry with
// its progress ring and count. Returns the width, laid out leftwards from `top_right`.
float ModernGalacticHud::build_queue(const GalacticHudFrame& f, Vec2f top_right, float height, float max_width) {
    if (!f.providers.build_queue) return 0.0f;
    f.providers.build_queue(queue_);
    if (queue_.empty()) return 0.0f;
    const float gap = 0.5f * kPad;
    const float side = 0.5f * (height - gap);
    const auto fit = static_cast<size_t>(std::max(1.0f, std::floor((max_width + gap) / (side + gap)) * 2.0f));
    if (queue_.size() > fit) queue_.resize(fit);
    const size_t cols = (queue_.size() + 1) / 2;
    const float width = static_cast<float>(cols) * side + static_cast<float>(cols - 1) * gap;
    const float left = top_right.x - width;
    for (size_t i = 0; i < queue_.size(); ++i) {
        const HudQueueEntry& e = queue_[i];
        Tile t;
        t.id = kModernGalacticQueueBase + static_cast<int32_t>(i);
        t.size = {side, side};
        t.c = {left + static_cast<float>(i / 2) * (side + gap) + 0.5f * side, top_right.y - static_cast<float>(i % 2) * (side + gap) - 0.5f * side};
        t.icon = e.icon;
        t.ring = std::clamp(e.progress, 0.0f, 1.0f);
        if (e.count > 1) t.badge = count_text(e.count);
        tile(t);
        ++stats_.queue_entries;
    }
    return width;
}

// The build tabs over the current tab's options (GalacticProviders::build_options at the planet). An inaccessible surface
// (land_tab_allowed false) leaves out the options Classic lists on its land tab, so Ground is empty then.
void ModernGalacticHud::build_options(const GalacticHudFrame& f, const GalacticPlanetInfo& p, Vec2f top_left, float height, float width) {
    std::vector<GalacticBuildOption> all;
    if (f.providers.build_options) f.providers.build_options(p.planet, all);
    if (!f.land_tab_allowed) std::erase_if(all, [](const GalacticBuildOption& o) { return !o.space; });
    for (const GalacticBuildOption& o : all) tab_live_[static_cast<size_t>(galactic_build_tab(o))] = true;
    const float th = text_height_units();
    const float tab_h = 1.6f * th, gap = 0.5f * kPad;
    const float tab_w = std::min(8.0f * th, (width - 3.0f * gap) / static_cast<float>(kGalacticBuildTabs));
    for (uint32_t k = 0; k < kGalacticBuildTabs; ++k) {
        Tile t;
        t.id = kModernGalacticTabBase + static_cast<int32_t>(k);
        t.size = {tab_w, tab_h};
        t.c = {top_left.x + static_cast<float>(k) * (tab_w + gap) + 0.5f * tab_w, top_left.y - 0.5f * tab_h};
        t.text = text_or_port(text_, nullptr, kTabLabels[k]);
        t.selected = static_cast<uint32_t>(tab_) == k;
        t.enabled = tab_live_[k] || t.selected;
        tile(t);
        if (tab_live_[k]) ++stats_.tabs_live;
    }
    stats_.tab = tab_;
    for (const GalacticBuildOption& o : all)
        if (galactic_build_tab(o) == tab_) options_shown_.push_back(o);
    const float side = height - tab_h - gap;
    const auto fit = static_cast<size_t>(std::max(0.0f, std::floor((width + gap) / (side + gap))));
    if (options_shown_.size() > fit) options_shown_.resize(fit);  // past the row: dropped, as Classic's full pad rows
    for (size_t i = 0; i < options_shown_.size(); ++i) {
        const GalacticBuildOption& o = options_shown_[i];
        HudBuildOption line;  // the shared cost / blocked-reason line
        line.cost = o.cost;
        line.block = o.block;
        Tile t;
        t.id = kModernGalacticBuildBase + static_cast<int32_t>(i);
        t.size = {side, side};
        t.c = {top_left.x + static_cast<float>(i) * (side + gap) + 0.5f * side, top_left.y - tab_h - gap - 0.5f * side};
        t.icon = o.icon;
        t.enabled = galactic_option_buyable(o);
        t.text = o.production_disabled ? text_or_port(text_, nullptr, U"Unavailable") : build_option_text(line, text_);
        if (o.queued > 0) t.badge = count_text(o.queued);
        tile(t);
        stats_.build_texts.push_back(t.text);
        ++stats_.build_options;
        if (!t.enabled) ++stats_.build_blocked;
    }
}

// #613 the map: a square panel at the deck's end (left, right with the map side option), level with its bottom edge. The
// Classic radar's blips on its mapping (galactic_radar_fit) - a planet in its owner's colour, the selected one framed in
// white (a shape cue, not colour alone) - and the camera's footprint (its bounding box, clipped, as Classic's).
void ModernGalacticHud::build_map(const GalacticHudFrame& f) {
    const float side = kDeckHeight;
    const float cx = options_.map_right ? 0.5f * canvas_.width - kMargin - 0.5f * side : -0.5f * canvas_.width + kMargin + 0.5f * side;
    const Vec2f center{cx, -0.5f * canvas_.height + kMargin + 0.5f * side}, size{side - 2.0f * kPad, side - 2.0f * kPad};
    frame_rect(dl_, kPanelLayer, center, {side, side}, colours_.back, accent(200));
    add_rect(dl_, kPanelLayer, center.x - 0.5f * size.x, center.y - 0.5f * size.y, center.x + 0.5f * size.x, center.y + 0.5f * size.y,
             Rgba8{0, 0, 0, 120});
    hit(kModernPanelHit, kPanelLayer, center, {side, side});
    hit(map_id(), kControlLayer, center, size);
    map_side_ = side;
    map_at_ = center;
    map_size_ = size;
    stats_.map = true;
    if (f.planets.empty()) return;
    map_fit_ = galactic_radar_fit(f.planets);
    const float r = std::max(2.0f, 0.025f * side);
    for (const GalacticPlanetInfo& p : f.planets) {
        if (!p.visible) continue;
        const Vec2f at = map_fit_.to_canvas(center, size, p.position.x, p.position.y);
        if (p.selected) add_rect(dl_, kControlLayer, at.x - r - 1.5f, at.y - r - 1.5f, at.x + r + 1.5f, at.y + r + 1.5f, colours_.text);
        add_rect(dl_, kControlLayer, at.x - r, at.y - r, at.x + r, at.y + r, Rgba8{p.owner_color.r, p.owner_color.g, p.owner_color.b, 255});
        ++stats_.map_blips;
    }
    const std::array<Vec2f, 4> fp = camera_footprint(f.view, map_fit_.z);
    float fx0 = fp[0].x, fx1 = fx0, fy0 = fp[0].y, fy1 = fy0;
    for (const Vec2f& c : fp) {
        fx0 = std::min(fx0, c.x);
        fx1 = std::max(fx1, c.x);
        fy0 = std::min(fy0, c.y);
        fy1 = std::max(fy1, c.y);
    }
    Vec2f a = map_fit_.to_canvas(center, size, fx0, fy0), b = map_fit_.to_canvas(center, size, fx1, fy1);
    const Vec2f lo{center.x - 0.5f * size.x, center.y - 0.5f * size.y}, hi{center.x + 0.5f * size.x, center.y + 0.5f * size.y};
    a = {std::clamp(a.x, lo.x, hi.x), std::clamp(a.y, lo.y, hi.y)};
    b = {std::clamp(b.x, lo.x, hi.x), std::clamp(b.y, lo.y, hi.y)};
    if (b.x - a.x < 1.0f || b.y - a.y < 1.0f) return;
    const Rgba8 line{255, 255, 255, 220};
    add_rect(dl_, kControlLayer + 1, a.x, a.y, b.x, a.y + 1.0f, line);
    add_rect(dl_, kControlLayer + 1, a.x, b.y - 1.0f, b.x, b.y, line);
    add_rect(dl_, kControlLayer + 1, a.x, a.y, a.x + 1.0f, b.y, line);
    add_rect(dl_, kControlLayer + 1, b.x - 1.0f, a.y, b.x, b.y, line);
    stats_.map_view = true;
}

// #613 the organiser above the deck (the planet close-up): a row per own fleet in orbit - its header, then its unit
// stacks - and the land-forces row at the bottom, the panel growing up from `bottom_left`.
void ModernGalacticHud::build_organiser(const GalacticHudFrame& f, const GalacticPlanetInfo& p, Vec2f bottom_left, float max_width) {
    constexpr size_t kRows = 5;       // fleet rows at most [port]
    constexpr size_t kLandMax = 20;   // the Classic land_fleet_00..19 pads
    std::vector<GalacticFleet> own;
    if (f.providers.fleets) f.providers.fleets(p.planet, own);
    std::erase_if(own, [](const GalacticFleet& g) { return g.enemy || g.fleet == 0 || g.fleet == commands::kNoObject; });
    // Slot order (the Classic panes q1 / q2 / q3 left to right); a fleet without a slot after them, in feed order.
    std::stable_sort(own.begin(), own.end(), [](const GalacticFleet& a, const GalacticFleet& b) {
        return (a.slot < 0 ? 3 : a.slot) < (b.slot < 0 ? 3 : b.slot);
    });
    if (own.size() > kRows) own.resize(kRows);
    std::vector<GalacticFleetSlot> land;
    if (f.providers.land_forces) f.providers.land_forces(p.planet, land);
    if (land.size() > kLandMax) land.resize(kLandMax);
    const float th = text_height_units();
    const float side = std::max(2.6f * th, px_units(48.0f)), gap = 0.5f * kPad, header_w = 7.0f * th;
    const float inner = max_width - 2.0f * kPad;
    const auto fit = static_cast<size_t>(std::max(0.0f, std::floor((inner - header_w) / (side + gap))));
    // Rows: the fleets, then the land forces; the panel is as wide as its longest row.
    std::vector<std::vector<GalacticUnitStack>> stacks(own.size());
    size_t longest = std::min(land.size(), fit);
    for (size_t i = 0; i < own.size(); ++i) {
        galactic_unit_stacks(own[i].units, stacks[i]);
        if (stacks[i].size() > fit) stacks[i].resize(fit);  // further stacks are not drawn, as a full Classic pane's
        longest = std::max(longest, stacks[i].size());
    }
    const size_t rows = own.size() + 1;
    const Vec2f size{std::min(max_width, header_w + static_cast<float>(longest) * (side + gap) + 2.0f * kPad),
                     static_cast<float>(rows) * side + static_cast<float>(rows - 1) * gap + 2.0f * kPad};
    const Vec2f center{bottom_left.x + 0.5f * size.x, bottom_left.y + 0.5f * size.y};
    frame_rect(dl_, kPanelLayer, center, size, colours_.back, accent(200));
    hit(kModernPanelHit, kPanelLayer, center, size);
    stats_.organiser = true;
    const float left = center.x - 0.5f * size.x + kPad;
    float y = center.y + 0.5f * size.y - kPad - 0.5f * side;
    auto pad = [&](ModernOrganiserPad pd, Tile t) {
        pd.id = t.id = kModernGalacticOrganiserBase + static_cast<int32_t>(org_.size());
        // The drag's target under the cursor that takes the drop: drawn selected (the Classic drop highlight).
        t.selected = t.selected || (drag_.active && hover_ == t.id && drag_.valid);
        tile(t);
        org_.push_back(std::move(pd));
    };
    for (size_t i = 0; i < own.size(); ++i, y -= side + gap) {
        const GalacticFleet& g = own[i];
        uint32_t n = 0;
        for (const GalacticFleetSlot& u : g.units) n += u.count;
        ModernOrganiserPad hp;
        hp.fleet = g.fleet;
        hp.select = {g.fleet};
        hp.icon = g.units.empty() ? std::string() : g.units.front().icon;
        hp.name = g.name;
        hp.count = n;
        Tile ht;
        ht.size = {header_w - gap, side};
        ht.c = {left + 0.5f * ht.size.x, y};
        ht.text = !g.name.empty() ? g.name : text_or_port(text_, nullptr, U"Fleet");
        ht.badge = count_text(n);
        pad(std::move(hp), std::move(ht));
        ++stats_.organiser_fleets;
        float x = left + header_w;
        for (GalacticUnitStack& s : stacks[i]) {
            Tile t;
            t.size = {side, side};
            t.c = {x + 0.5f * side, y};
            t.icon = s.slot.icon;
            t.selected = s.slot.selected;
            if (s.slot.count > 1) t.badge = count_text(s.slot.count);
            ModernOrganiserPad sp;
            sp.fleet = g.fleet;
            sp.unit = s.slot.object;
            sp.select = std::move(s.members);
            sp.icon = s.slot.icon;
            sp.name = s.slot.name;
            sp.count = s.slot.count;
            pad(std::move(sp), std::move(t));
            ++stats_.organiser_stacks;
            x += side + gap;
        }
    }
    // The land forces: its back takes drops (land) with or without units; a tile per unit.
    const Vec2f row_c{center.x, y}, row_s{size.x - kPad, side + gap};
    frame_rect(dl_, kPanelLayer, row_c, row_s, Rgba8{0, 0, 0, 90},
               drag_.active && hover_ == kModernGalacticLandRowHit && drag_.valid ? colours_.text : accent(120));
    hit(kModernGalacticLandRowHit, kPanelLayer, row_c, row_s);
    land_row_ = true;
    HudTextStyle ls;
    ls.font = font();
    ls.color = colours_.label;
    const std::u32string label = text_or_port(text_, nullptr, U"Land forces");
    ls.width_scale = fit_width_scale(measure_text_block(*fonts_, canvas_, label, 0.0f, ls).width, header_w - kPad);
    add_text(dl_, kPanelLayer, HudList::Text, *fonts_, canvas_, label, {left, y}, ls);
    float x = left + header_w;
    for (size_t k = 0; k < land.size() && k < fit; ++k) {
        const GalacticFleetSlot& u = land[k];
        Tile t;
        t.size = {side, side};
        t.c = {x + 0.5f * side, y};
        t.icon = u.icon;
        t.selected = u.selected;
        if (u.count > 1) t.badge = count_text(u.count);
        ModernOrganiserPad lp;
        lp.land = true;
        lp.unit = u.object;
        lp.select = {u.object};
        lp.icon = u.icon;
        lp.name = u.name;
        lp.count = u.count;
        pad(std::move(lp), std::move(t));
        ++stats_.land_pads;
        x += side + gap;
    }
}

// #613 the dragged tile's icon follows the cursor on the topmost layer (as Classic's Should_Render_At_Drag_Pos).
void ModernGalacticHud::build_drag() {
    stats_.drop_valid = drag_.active && drag_.valid;
    if (!drag_.active || !atlas_) return;
    const Vec2f at = canvas_.pixel_to_canvas(drag_.cursor_px.x, drag_.cursor_px.y);
    const float s = px_units(40.0f);
    if (drag_.icon.empty() || !add_atlas_quad(dl_, kHudLayers - 1, HudList::Alpha, *atlas_, drag_.icon, at, {s, s}, 1.0f, Rgba8{255, 255, 255, 200}))
        frame_rect(dl_, kHudLayers - 1, at, {s, s}, accent(150), colours_.text);
}

const ModernOrganiserPad* ModernGalacticHud::org_pad(int32_t id) const {
    if (id < kModernGalacticOrganiserBase || id - kModernGalacticOrganiserBase >= static_cast<int32_t>(org_.size())) return nullptr;
    return &org_[static_cast<size_t>(id - kModernGalacticOrganiserBase)];
}

GalacticDropTarget ModernGalacticHud::drop_target(int32_t component, Vec2f px) const {
    using Target = GalacticDropTarget;
    if (component >= 0) {
        if (const ModernOrganiserPad* p = org_pad(component))  // a fleet's row (header or stack), or a land unit
            return p->land ? Target{Target::Kind::LandForces} : Target{Target::Kind::Fleet, p->fleet, false};
        if (component == kModernGalacticLandRowHit && land_row_) return Target{Target::Kind::LandForces};
        return Target{Target::Kind::Other};  // any other Modern control, or the Classic map layer's
    }
    Target t{Target::Kind::World};
    const int32_t pi = galactic_planet_at(planet_px_, px.x, px.y);
    t.on_planet = pi >= 0;
    if (pi >= 0) t.planet = planet_px_[static_cast<size_t>(pi)].planet;
    return t;
}

bool ModernGalacticHud::drop(std::vector<commands::EventData>* out) const {
    const GalacticOrganiserDrag source{drag_.fleet_index >= 0, drag_.fleet, drag_.unit, 0};
    return galactic_organiser_drop(source, drop_target(drag_.over_component, drag_.cursor_px), selected_id_, out);
}

void ModernGalacticHud::update_input(const RoutedInput& routed, bool shift, bool ctrl, double now,
                                     std::vector<commands::EventData>& out, std::vector<HudAction>* actions) {
    hover_ = routed.hover_component;
    if (!table_) return;
    auto act = [&](HudAction a) {
        if (actions) actions->push_back(std::move(a));
    };
    // #613 the organiser drag (GalacticHud::drag_update's rules): a left press on a tile, then a move past the threshold
    // while the HUD owns the button; the release drops it (and swallows that release's click).
    bool dropped = false;
    for (const HudPointerEvent& e : routed.hud) {
        if (e.button != Button::Left) continue;
        if (e.kind == HudPointerEvent::Kind::Press) {
            press_pad_ = org_pad(e.component) ? e.component : -1;
            press_px_ = {static_cast<float>(e.x), static_cast<float>(e.y)};
        } else if (e.kind == HudPointerEvent::Kind::Release) {
            if (drag_.active) {
                drag_.cursor_px = {static_cast<float>(e.x), static_cast<float>(e.y)};
                drag_.over_component = e.component;
                drop(&out);
                dropped = true;
            }
            press_pad_ = -1;
            drag_ = {};
        }
    }
    const Vec2f cur{static_cast<float>(routed.world.mouse_x), static_cast<float>(routed.world.mouse_y)};
    if (press_pad_ >= 0 && !drag_.active && routed.owner[static_cast<size_t>(Button::Left)] == PointerTarget::Hud &&
        std::hypot(cur.x - press_px_.x, cur.y - press_px_.y) > static_cast<float>(GalacticHud::kDragThresholdPx))
        if (const ModernOrganiserPad* p = org_pad(press_pad_)) {
            drag_.active = true;
            drag_.component = p->id;
            drag_.fleet_index = p->land ? -1 : 0;
            drag_.fleet = p->fleet;
            drag_.unit = p->unit;
            drag_.icon = p->icon;
        }
    if (drag_.active) {
        drag_.cursor_px = cur;
        drag_.over_component = routed.hover_component;
        const GalacticDropTarget t = drop_target(drag_.over_component, cur);
        drag_.over_fleet = t.kind == GalacticDropTarget::Kind::Fleet && t.fleet != drag_.fleet;
        drag_.over_planet = -1;
        if (t.kind == GalacticDropTarget::Kind::World && t.on_planet)
            for (const GalacticPlanetScreen& s : planet_px_)
                if (s.planet == t.planet) drag_.over_planet = static_cast<int32_t>(s.index);
        drag_.valid = drop(nullptr);
    }
    if (dropped) return;  // the release ended a drag, not a click
    auto index = [](int32_t component, int32_t base, size_t n) {
        return component >= base && component - base < static_cast<int32_t>(n) ? static_cast<size_t>(component - base) : SIZE_MAX;
    };
    for (const HudPointerEvent& e : routed.hud) {
        if (e.kind != HudPointerEvent::Kind::Click || e.component < 0 || !drawn(e.component)) continue;
        // #613 the map: the Classic radar's click (RadarCameraJump to that point).
        if (e.component == map_id()) {
            HudAction a;
            const Vec2f c = canvas_.pixel_to_canvas(static_cast<float>(e.x) + 0.5f, static_cast<float>(e.y) + 0.5f);
            if (galactic_radar_click(map_fit_, map_at_, map_size_, c, e.component,
                                     radar_ >= 0 ? (*table_)[static_cast<size_t>(radar_)].name : std::string(), e.button, a))
                act(std::move(a));
            continue;
        }
        // #613 an organiser tile: the Classic pad's click (GalacticHud::update_input): GalacticSelect of the pad, and a second
        // plain left click on the same tile within 0.5 s and 4 px asks for a rename of the fleet (header) or the unit.
        if (const ModernOrganiserPad* p = org_pad(e.component)) {
            act(galactic_select_action(e.component, p->select, e.button, shift, ctrl));
            if (e.button == Button::Left && !shift && !ctrl) {
                const bool dbl = last_pad_click_ == e.component && now - last_pad_time_ <= 0.5 && std::abs(e.x - last_pad_x_) <= 4 &&
                                 std::abs(e.y - last_pad_y_) <= 4;
                if (dbl && (p->unit != 0 || p->fleet != 0)) {
                    HudAction r;
                    r.kind = HudAction::Kind::Rename;
                    r.component = e.component;
                    r.unit = p->unit != 0 ? p->unit : p->fleet;
                    r.value = p->unit != 0 ? 0 : 1;
                    if (p->fleet != 0) r.ids = {p->fleet};
                    act(std::move(r));
                    last_pad_click_ = -1;
                } else {
                    last_pad_click_ = e.component;
                    last_pad_time_ = now;
                    last_pad_x_ = e.x;
                    last_pad_y_ = e.y;
                }
            }
            continue;
        }
        if (view_click(e, actions)) continue;  // the style switch, Jump
        if (const size_t k = index(e.component, kModernGalacticTabBase, kGalacticBuildTabs); k != SIZE_MAX) {
            if (e.button == Button::Left && tab_live_[k]) tab_ = static_cast<GalacticBuildTab>(k);
            continue;
        }
        // The Classic build pad clicks (GalacticHud::update_input): a greyed option refuses with the GUI negative feedback.
        if (const size_t k = index(e.component, kModernGalacticBuildBase, options_shown_.size()); k != SIZE_MAX) {
            const GalacticBuildOption& o = options_shown_[k];
            if (e.button == Button::Left && !galactic_option_buyable(o)) act(galactic_refused_action(e.component, {}, o));
            else galactic_option_click(o, e.button, selected_id_, out);
            continue;
        }
        if (const size_t k = index(e.component, kModernGalacticQueueBase, queue_.size()); k != SIZE_MAX) {
            if (e.button == Button::Left) out.push_back(galactic_queue_cancel(queue_[k]));
            continue;
        }
        // An own fleet: the organiser header's selection (other players' fleets are inert, as their panes).
        if (const size_t k = index(e.component, kModernGalacticFleetBase, fleets_.size()); k != SIZE_MAX) {
            if (!fleets_[k].enemy) act(galactic_select_action(e.component, {fleets_[k].fleet}, e.button, shift, ctrl));
            continue;
        }
        // A hero row: the hero bar's clicks (HeroPanel::update_input, galactic; left only): HeroFocusPlanet, and on a
        // second click on the same row within 0.5 s and 4 px HeroZoomPlanet too.
        if (const size_t k = index(e.component, kModernGalacticHeroBase, heroes_.size()); k != SIZE_MAX) {
            if (e.button != Button::Left) continue;
            const bool dbl = last_hero_click_ == e.component && now - last_hero_time_ <= 0.5 && std::abs(e.x - last_hero_x_) <= 4 &&
                             std::abs(e.y - last_hero_y_) <= 4;
            last_hero_click_ = dbl ? -1 : e.component;
            last_hero_time_ = now;
            last_hero_x_ = e.x;
            last_hero_y_ = e.y;
            const uint32_t planet = hero_focus_planet(heroes_[k]);
            if (planet == 0) continue;
            for (const HudAction::Kind kind : {HudAction::Kind::HeroFocusPlanet, HudAction::Kind::HeroZoomPlanet}) {
                if (kind == HudAction::Kind::HeroZoomPlanet && !dbl) break;
                HudAction h;
                h.kind = kind;
                h.component = e.component;
                h.unit = heroes_[k].object;
                h.ids = {planet};
                h.button = e.button;
                h.shift = shift;
                h.ctrl = ctrl;
                act(std::move(h));
            }
            continue;
        }
        if (e.component >= kModernHitBase) continue;  // a panel's back
        // The shared Classic controls: pause through the command stream, planet_info -> PlanetInfo, the others as a
        // Button action by component name.
        if (e.component == pause_ && e.button == Button::Left) {
            out.push_back(pause_event(!paused_));
            continue;
        }
        HudAction h = button_action(e, shift, ctrl);
        if (e.component == planet_info_) {
            h.kind = HudAction::Kind::PlanetInfo;
            h.unit = selected_id_;
        }
        act(std::move(h));
    }
}

}  // namespace alamo::ui
