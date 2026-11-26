#include "alamo/ui/modern_tactical_hud.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <tuple>

#include "alamo/core/ascii.hpp"
#include "alamo/ui/gui_menus.hpp"
#include "alamo/ui/hud_font.hpp"
#include "alamo/ui/orders.hpp"
#include "alamo/ui/picking.hpp"
#include "modern_hud_draw.hpp"
#include "ui_strings.hpp"

namespace alamo::ui {

namespace {

using namespace modern_draw;

constexpr Rgba8 kBarBack{0, 0, 0, 170};
constexpr Rgba8 kPausedBar{196, 176, 8, 245};  // the Classic pause banner's yellow bar
constexpr float kDeckHeight = 120.0f;  // canvas units
// c_button00..05: attack, attack-move, move, waypoint, full stop, guard (HudOrderButtons), their port labels and hotkeys.
constexpr const char32_t* kOrderLabels[kOrderButtonCount] = {U"Attack", U"Attack Move", U"Move", U"Waypoint", U"Stop", U"Guard"};
constexpr uint16_t kOrderSlots[kOrderButtonCount] = {hk::kAttack, hk::kAttackMove, hk::kMove, hk::kWaypoint, hk::kStop, hk::kGuard};

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
    init_view(table, atlas, fonts, text);
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
}

Rgba8 ModernTacticalHud::hull_colour(float health) const {
    return health > kModernHullWarn ? colours_.hull_ok : health > kModernHullCritical ? colours_.hull_warn : colours_.hull_bad;
}

void ModernTacticalHud::build(const TacticalHudFrame& frame, std::span<const HudUnit> units, std::span<const HudTypeInfo> types) {
    begin_build(frame.faction_alt);
    groups_.clear();
    abilities_.clear();
    buy_slots_.clear();
    queue_slots_.clear();
    reinforce_slots_.clear();
    roster_groups_.clear();
    roster_new_live_ = false;
    time_ = frame.time;
    fleet_tab_ids_.clear();
    fleet_tab_group_.clear();
    builder_ = frame.builder;
    discipline_drawn_ = false;
    roster_right_ = -0.5f * canvas_.width;  // no roster
    station_ = commands::kNoObject;
    map_view_ = {};
    radar_models_.clear();
    ground_z_ = frame.ground_z;
    // Classic's radar move click needs its selection groups (TacticalHud::build: its grouping and slot count).
    select_groups_ = !(frame.fleet_groups_on ? group_selection_fleets(units, frame.fleet_groups, panel_slots_) : group_selection(units)).empty();
    wing_ids_.clear();
    for (size_t w = 0; w < frame.wings.size() && w < kMaxWings; ++w)  // as Classic's wing buttons (an empty wing: none)
        wing_ids_.push_back(frame.wings[w].squadrons > 0 ? frame.wings[w].ids : std::vector<uint32_t>{});
    for (bool& l : order_live_) l = false;
    stats_ = {};
    paused_ = frame.paused;
    if (!table_ || !fonts_ || frame.world.interface_hidden || font() < 0) return;
    build_deck(frame, units, types);
    build_map(frame, units, types);
    build_badges(frame, units, types);
    build_target_card(frame, units, types);
    build_status(frame);
    build_roster(frame, units, types);
    build_move_readout(frame, units);  // after the roster: kept to its right
    build_controls(frame);
    const ModernAlertStrip strip = alert_strip(frame.alerts);
    stats_.alert_text = strip.text;
    stats_.alerts_more = strip.more;
    stats_.jump = strip.jump;
    build_objectives(frame, controls_bottom_ - kPad);
    build_pause(frame);
    stats_.key_badges += key_badges_;
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
    status_bottom_ = 0.5f * canvas_.height - kMargin;
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
    status_bottom_ = center.y - 0.5f * size.y;
    stats_.status_corner = true;
}

void ModernTacticalHud::build_controls(const TacticalHudFrame& f) {
    const float h = text_height_units() * 2.0f;
    const float hw = 0.5f * canvas_.width, hh = 0.5f * canvas_.height;
    const float y = hh - kMargin - 0.5f * h;
    controls_bottom_ = y - 0.5f * h;
    float right = hw - kMargin;  // laid out right to left
    auto button = [&](int32_t id, std::u32string_view label, bool selected) {
        right -= control_button(id, label, selected, right, y, h) + 0.5f * kPad;
        ++stats_.controls;
    };
    // Menu, pause and fast forward: the Classic components (b_option_t, b_play_pause_t, b_fast_forward_t).
    if (option_ >= 0) button(option_, text_or_port(text_, nullptr, U"Menu"), false);
    if (pause_ >= 0) button(pause_, f.paused ? U">" : U"||", f.paused);
    if (fast_forward_ >= 0) button(fast_forward_, U">>", false);
    right -= kPad;
    style_switch(right, y, h);
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
        if (const float dw = build_discipline(f, units, {section_right, top}, height); dw > 0.0f) section_right -= dw + 2.0f * kPad;
        const float abilities_w = build_abilities(f, units, {section_right, top}, height, 0.3f * (section_right - left));
        if (abilities_w > 0.0f) section_right -= abilities_w + 2.0f * kPad;
    }
    // The left section: the reinforcement list while open, else production with a builder, else the selection cards.
    // A land pad's buttons stay on the pad whatever the deck shows (Classic draws them whenever a builder is selected).
    const bool pad = producing && f.world.land && f.has_build_anchor;
    if (reinforcing && build_reinforcements(f, {left, top}, height, section_right - left) > 0.0f) {
        if (pad) build_pad_options(f);
        return;
    }
    if (producing && build_production(f, units, types, {left, top}, height, section_right - left) > 0.0f) return;
    if (own) build_selection(f, units, types, {left, top}, section_right - left, height);
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
    std::vector<Entry> entries;  // the station (the wings and fleet groups are the roster's, #601)
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
    // Header: Clear at the right end (the wings are the roster's, #601).
    const float hy = top_left.y - 0.5f * header;
    {
        const std::u32string clear = text_or_port(text_, nullptr, U"Clear");
        const float cw = measure_text_block(*fonts_, canvas_, clear, 0.0f, centred).width + 2.0f * kPad;
        const Vec2f c{top_left.x + width - 0.5f * cw, hy};
        frame_rect(dl_, kControlLayer, c, {cw, header}, colours_.button, hover_ == kModernClearHit ? colours_.text : accent(200));
        add_text(dl_, kControlLayer, HudList::Text, *fonts_, canvas_, clear, c, centred);
        hit(kModernClearHit, kControlLayer, c, {cw, header});
        stats_.clear_button = true;
    }
    // Cards: the Classic grouping; with more groups than fit, the last card is "+N" (every unit not shown). A row of tabs
    // over the cards with fleet groups on (#588) or a wing's craft selected (#600), tall enough for 24 px Merge / Split.
    bool wing_selected = false;
    for (const HudWing& w : f.wings)
        for (const HudUnit& u : units)
            wing_selected = wing_selected || (u.selected && std::binary_search(w.ids.begin(), w.ids.end(), u.id));
    const float tab_h = f.fleet_groups_on || wing_selected ? std::max(line * 1.6f, px_units(24.0f) + 8.0f) : 0.0f;
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
    if (tab_h > 0.0f) build_run_tabs(f, units, {{top_left.x + 0.5f * card_w, cy}, {card_w, card_h}, gap, top_left.x + width}, tab_h);
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

void ModernTacticalHud::chip_icon(Chip chip, Vec2f c, float u, Rgba8 edge, uint32_t layer) {
    const float ix = c.x, iy = c.y;
    switch (chip) {
        case Chip::Locked:  // a closed padlock: the shackle down on the body
        case Chip::Free: {  // an open padlock: the shackle raised and off to one side
            const float lift = chip == Chip::Free ? 0.5f * u : 0.0f, shift = chip == Chip::Free ? 0.35f * u : 0.0f;
            frame_rect(dl_, layer, {ix + shift, iy + 0.45f * u + lift}, {u, u}, Rgba8{0, 0, 0, 0}, edge);
            add_rect(dl_, layer, ix - 0.6f * u, iy - 0.9f * u, ix + 0.6f * u, iy + 0.05f * u, edge);
            break;
        }
        case Chip::Wing: {  // a wing: a swept triangle
            HudQuad q;
            q.has_corners = true;
            q.corners = {Vec2f{ix - 0.7f * u, iy - 0.6f * u}, Vec2f{ix + 0.8f * u, iy + 0.7f * u}, Vec2f{ix + 0.8f * u, iy + 0.7f * u},
                         Vec2f{ix + 0.2f * u, iy - 0.6f * u}};
            q.color = edge;
            dl_.add(layer, HudList::Alpha, q);
            break;
        }
        case Chip::Lagging: {  // #603 a warning triangle with a dark "!"
            HudQuad q;
            q.has_corners = true;
            q.corners = {Vec2f{ix - u, iy - 0.85f * u}, Vec2f{ix, iy + 0.95f * u}, Vec2f{ix, iy + 0.95f * u}, Vec2f{ix + u, iy - 0.85f * u}};
            q.color = edge;
            dl_.add(layer, HudList::Alpha, q);
            add_rect(dl_, layer, ix - 0.1f * u, iy - 0.25f * u, ix + 0.1f * u, iy + 0.45f * u, Rgba8{0, 0, 0, 230});
            add_rect(dl_, layer, ix - 0.1f * u, iy - 0.65f * u, ix + 0.1f * u, iy - 0.45f * u, Rgba8{0, 0, 0, 230});
            break;
        }
    }
}

namespace {
// The unit with this id (units ascending by id), or null.
const HudUnit* find_unit(std::span<const HudUnit> units, uint32_t id) {
    const auto it = std::lower_bound(units.begin(), units.end(), id, [](const HudUnit& u, uint32_t v) { return u.id < v; });
    return it != units.end() && it->id == id ? &*it : nullptr;
}
}  // namespace

std::u32string ModernTacticalHud::lag_text(uint32_t n, bool words) const {
    return words ? count_text(n) + U" " + text_or_port(text_, nullptr, U"lagging") : count_text(n);
}

float ModernTacticalHud::lag_mark_width(uint32_t n, bool words) {
    HudTextStyle st;
    st.font = font();
    return 0.8f * text_height_units() + 0.5f * kPad + measure_text_block(*fonts_, canvas_, lag_text(n, words), 0.0f, st).width;
}

float ModernTacticalHud::lag_mark(Vec2f right_mid, uint32_t n, bool words, uint32_t layer) {
    HudTextStyle st;
    st.font = font();
    st.color = colours_.hull_warn;
    st.align = TextAlign::Right;
    const float w = lag_mark_width(n, words), u = 0.4f * text_height_units();
    chip_icon(Chip::Lagging, {right_mid.x - w + u, right_mid.y}, u, colours_.hull_warn, layer);
    add_text(dl_, layer, HudList::Text, *fonts_, canvas_, lag_text(n, words), right_mid, st);
    return w;
}

// #588 / #600 the run tabs over the selection cards: one per fleet group run (its number and name, Merge / Split by the
// shared rules) and one per wing run (cards whose units are all one wing's: its name and key). Each tab has a state chip
// (a closed padlock LOCKED, an open one FREE, a wing WING: a shape as well as the text) and an aggregate health bar
// (the feed's, else the run's cards' mean). A group tab clicks as Classic's tab (kFleetTabHitBase + index), a wing tab
// as Classic's wing button (WingSelect).
void ModernTacticalHud::build_run_tabs(const TacticalHudFrame& f, std::span<const HudUnit> units, const CardRow& row, float tab_h) {
    const float card_w = row.card.x, card_h = row.card.y, gap = row.gap;
    const Vec2f first_card = row.first;
    const std::vector<size_t> selected = fleet_selected_counts(units, f.fleet_groups);
    HudTextStyle st;
    st.font = font();
    st.color = colours_.text;
    st.align = TextAlign::Left;
    HudTextStyle bs = st;
    bs.align = TextAlign::Center;
    auto wing_of = [&](const SelectedGroup& g) -> int32_t {  // the wing every unit of the card belongs to, -1 none
        for (size_t w = 0; w < f.wings.size() && w < kMaxWings; ++w) {
            const std::vector<uint32_t>& ids = f.wings[w].ids;
            if (!g.ids.empty() && std::all_of(g.ids.begin(), g.ids.end(), [&](uint32_t id) { return std::binary_search(ids.begin(), ids.end(), id); }))
                return static_cast<int32_t>(w);
        }
        return -1;
    };
    const std::u32string merge = text_or_port(text_, nullptr, U"Merge"), split = text_or_port(text_, nullptr, U"Split");
    // Merge / Split: at least 24 px, from the tab's top down to just above the health bar along its bottom edge.
    const float button_h = std::max(px_units(24.0f), tab_h - 7.0f);
    for (size_t first = 0; first < groups_.size();) {
        const SelectedGroup& g0 = groups_[first];
        const uint16_t id = g0.fleet;
        const int32_t wing = id == 0 && !g0.overflow ? wing_of(g0) : -1;
        size_t end = first + 1;  // the run [first, end): one group's or one wing's cards
        if (id != 0)
            while (end < groups_.size() && groups_[end].fleet == id && !groups_[end].overflow) ++end;
        else if (wing >= 0)
            while (end < groups_.size() && groups_[end].fleet == 0 && !groups_[end].overflow && wing_of(groups_[end]) == wing) ++end;
        if (g0.overflow || (id == 0 && wing < 0)) {
            first = end;
            continue;
        }
        const size_t s = first, e = end;
        const HudFleetTabState ts = id != 0 ? fleet_tab_state(f.fleet_groups, selected, id) : HudFleetTabState{};
        const bool locked = ts.group && ts.group->locked;
        const Chip chip = wing >= 0 ? Chip::Wing : locked ? Chip::Locked : Chip::Free;
        const Rgba8 edge = locked ? colours_.hull_warn : accent(230);
        const float x0 = first_card.x - 0.5f * card_w + static_cast<float>(s) * (card_w + gap);
        const float x1 = first_card.x - 0.5f * card_w + static_cast<float>(e - 1) * (card_w + gap) + card_w;
        // The run's frame (round the cards) and its tab.
        frame_rect(dl_, kControlLayer, {0.5f * (x0 + x1), first_card.y}, {x1 - x0 + 4.0f, card_h + 4.0f}, Rgba8{0, 0, 0, 0}, edge);
        std::u32string label;
        if (wing >= 0) {
            const HudWing& w = f.wings[static_cast<size_t>(wing)];
            label = w.label;
            if (!w.hotkey.empty() && options_.key_hints) label += U" " + w.hotkey;  // as the wing button's key hint
        } else {
            char num[16];
            std::snprintf(num, sizeof num, "%u", static_cast<unsigned>(id));
            label = ascii32(num);
            if (ts.group && !ts.group->name.empty()) label += U" " + ts.group->name;
        }
        const std::u32string chip_text = text_or_port(text_, nullptr, chip == Chip::Wing ? U"WING" : chip == Chip::Locked ? U"LOCKED" : U"FREE");
        const float chip_full = 0.6f * tab_h + measure_text_block(*fonts_, canvas_, chip_text, 0.0f, bs).width + kPad;
        const float mw = measure_text_block(*fonts_, canvas_, merge, 0.0f, bs).width + kPad;
        const float sw = measure_text_block(*fonts_, canvas_, split, 0.0f, bs).width + kPad;
        const float buttons = (ts.merge ? mw + 2.0f : 0.0f) + (ts.split ? sw + 2.0f : 0.0f);
        // #603 "N lagging" before the buttons (just the triangle and N when short of room)
        const uint32_t lag = ts.group ? static_cast<uint32_t>(ts.group->lagging.size()) : 0u;
        const float lag_full = lag > 0 ? lag_mark_width(lag, true) + kPad : 0.0f, lag_short = lag > 0 ? lag_mark_width(lag, false) + kPad : 0.0f;
        // As wide as the run, or its chip, label and buttons, up to the next run's start (or the section's end): the next tab
        // never covers this one's buttons; the label is squeezed to the room left.
        const float room = (e < groups_.size() ? first_card.x - 0.5f * card_w + static_cast<float>(e) * (card_w + gap) - 0.5f * gap : row.right) - (x0 - 2.0f);
        const float label_w = measure_text_block(*fonts_, canvas_, label, 0.0f, st).width;
        const float tab_w = std::max(x1 - x0 + 4.0f, std::min(room, chip_full + label_w + lag_full + buttons + 3.0f * kPad));
        // the words when they fit beside the icon-only chip, else the short form; neither (a one-card run with Merge and
        // Split): no mark on the tab, the roster card still has it
        const float lag_room = tab_w - buttons - 3.0f * kPad - 0.8f * tab_h;
        const bool lag_words = lag > 0 && lag_room >= lag_full, lag_shown = lag > 0 && lag_room >= lag_short;
        const float lag_w = !lag_shown ? 0.0f : lag_words ? lag_full : lag_short;
        // Short of room (a one-card run with Merge / Split): the chip keeps its icon only, then the label is squeezed (at
        // most to half width) or left out, so nothing runs under the buttons.
        const float avail = tab_w - buttons - 3.0f * kPad - lag_w;
        const bool chip_label = chip_full + 0.5f * label_w <= avail;
        const float chip_w = chip_label ? chip_full : 0.8f * tab_h;
        const float label_room = avail - chip_w;
        const bool show_label = label_room >= 0.5f * label_w && label_room > 0.0f;
        st.width_scale = fit_width_scale(label_w, std::max(1.0f, label_room));
        const Vec2f tab{x0 - 2.0f + 0.5f * tab_w, first_card.y + 0.5f * card_h + 2.0f + 0.5f * tab_h};
        const int32_t index = static_cast<int32_t>(fleet_tab_ids_.size());
        const int32_t tab_id = wing >= 0 ? kModernWingTabBase + wing : kFleetTabHitBase + index;
        frame_rect(dl_, kControlLayer, tab, {tab_w, tab_h}, colours_.button, hover_ == tab_id ? colours_.text : edge);
        // The chip: a dark plate with its icon and text.
        float tx = tab.x - 0.5f * tab_w + 0.5f * kPad;
        const Vec2f cc{tx + 0.5f * chip_w, tab.y + 1.0f};
        frame_rect(dl_, kControlLayer, cc, {chip_w, tab_h - 8.0f}, Rgba8{0, 0, 0, 200}, edge);
        const float u = 0.22f * tab_h, ix = cc.x - 0.5f * chip_w + 0.5f * kPad + 0.5f * u, iy = cc.y;
        chip_icon(chip, {ix, iy}, u, edge, kControlLayer);
        if (chip == Chip::Locked) ++stats_.chips_locked;
        else if (chip == Chip::Free) ++stats_.chips_free;
        else ++stats_.chips_wing;
        if (chip_label) add_text(dl_, kControlLayer, HudList::Text, *fonts_, canvas_, chip_text, {cc.x + 0.3f * tab_h, cc.y}, bs);
        tx += chip_w + kPad;
        if (show_label) add_text(dl_, kControlLayer, HudList::Text, *fonts_, canvas_, label, {tx, tab.y + 1.0f}, st);
        if (lag_shown) {
            lag_mark({tab.x + 0.5f * tab_w - buttons - 0.5f * kPad, tab.y + 1.0f}, lag, lag_words, kControlLayer);
            ++stats_.tab_lagging;
        }
        // The aggregate health along the tab's bottom edge (solid, as a hull bar).
        float health = wing >= 0 ? f.wings[static_cast<size_t>(wing)].health : ts.group ? ts.group->health : -1.0f;
        if (health < 0.0f) {  // the run's cards, by unit count
            float sum = 0.0f;
            size_t n = 0;
            for (size_t k = s; k < e; ++k) {
                sum += groups_[k].health * static_cast<float>(groups_[k].ids.size());
                n += groups_[k].ids.size();
            }
            health = n > 0 ? sum / static_cast<float>(n) : -1.0f;
        }
        if (health >= 0.0f) {
            add_bar(dl_, kControlLayer, {tab.x, tab.y - 0.5f * tab_h + 2.5f}, {tab_w - 4.0f, 3.0f}, health, hull_colour(health), false);
            ++stats_.tab_health_bars;
        }
        hit(tab_id, kControlLayer, tab, {tab_w, tab_h});
        float bx = tab.x + 0.5f * tab_w - 1.0f;
        auto button = [&](int32_t base, const std::u32string& text, float w) {  // a layer over the tabs, as Classic's
            const Vec2f bc{bx - 0.5f * w, tab.y + 0.5f * tab_h - 1.0f - 0.5f * button_h};
            bx -= w + 2.0f;
            frame_rect(dl_, kControlLayer + 1, bc, {w, button_h}, accent(120), hover_ == base + index ? colours_.text : edge);
            add_text(dl_, kControlLayer + 1, HudList::Text, *fonts_, canvas_, text, bc, bs);
            hit(base + index, kControlLayer + 1, bc, {w, button_h});
        };
        if (ts.split) {
            button(kFleetSplitHitBase, split, sw);
            ++stats_.fleet_split;
        }
        if (ts.merge) {
            button(kFleetMergeHitBase, merge, mw);
            ++stats_.fleet_merge;
        }
        st.width_scale = 1.0f;
        first = end;
        if (wing >= 0) {
            ++stats_.wing_tabs;
            continue;
        }
        std::vector<uint32_t> members;
        if (ts.group) members = ts.group->members;
        else
            for (size_t k = s; k < e; ++k) members.insert(members.end(), groups_[k].ids.begin(), groups_[k].ids.end());
        fleet_tab_ids_.push_back(std::move(members));
        fleet_tab_group_.emplace_back(id, ts.group ? utf32_to_utf8(ts.group->name) : std::string{});
        ++stats_.fleet_tabs;
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

// #602 a badge: the text on a dark backplate (1.1 x the text height), its left edge's middle at left_mid. #603 lagging:
// an amber edge and a warning triangle before the text.
void ModernTacticalHud::badge(std::u32string_view text, Vec2f left_mid, uint32_t layer, bool lagging) {
    HudTextStyle st;
    st.font = font();
    st.color = colours_.text;
    st.align = TextAlign::Center;
    const float h = 1.1f * text_height_units(), u = 0.35f * h, icon = lagging ? 2.0f * u + 0.5f * kPad : 0.0f;
    const float w = std::max(h, measure_text_block(*fonts_, canvas_, text, 0.0f, st).width + kPad) + icon;
    const Vec2f c{left_mid.x + 0.5f * w, left_mid.y};
    frame_rect(dl_, layer, c, {w, h}, Rgba8{0, 0, 0, 210}, lagging ? colours_.hull_warn : colours_.label);
    if (lagging) chip_icon(Chip::Lagging, {left_mid.x + 0.25f * kPad + u, c.y}, u, colours_.hull_warn, layer);
    add_text(dl_, layer, HudList::Text, *fonts_, canvas_, text, {c.x + 0.5f * icon, c.y}, st);
}

// #602 (#596 world markers), Modern only: a group badge (the group's number, or the wing's key F2 / F3) on a dark
// backplate beside each selected own unit's world icon (unit_icon_centre: a squadron's one icon stands for its craft), the
// same badges on the map at each group's and wing's member centre, and for each selected locked group (the Classic
// outline's rule, #178) a dark edge round the outline and a tag over it: a padlock, the number and name, LOCKED.
// #616 the same badge in the top-left corner of each hero bar slot whose hero is in a group or wing, one layer over the
// slot (the bar is Classic's panel; Modern draws over it and Classic's list is untouched).
namespace {
// A wing's badge: its key (F2 / F3), else its label's first letter.
std::u32string wing_badge(const HudWing& w) { return w.hotkey.empty() ? w.label.substr(0, 1) : w.hotkey; }
}  // namespace

void ModernTacticalHud::build_badges(const TacticalHudFrame& f, std::span<const HudUnit> units, std::span<const HudTypeInfo> types) {
    if (f.world.interface_hidden || f.world.cinematic) return;
    constexpr uint32_t kWorldLayer = 2;  // under the panels
    auto label_of = [&](uint32_t id) -> std::u32string {
        if (f.fleet_groups_on)
            for (const HudFleetGroup& g : f.fleet_groups)
                if (std::binary_search(g.members.begin(), g.members.end(), id)) return count_text(g.id);
        for (const HudWing& w : f.wings)
            if (std::binary_search(w.ids.begin(), w.ids.end(), id)) return wing_badge(w);
        return {};
    };
    // Beside the world icons.
    for (const HudUnit& u : units) {
        if (!u.selected || u.owner != f.world.local_player || u.type >= types.size()) continue;
        if (u.squadron_member && !u.unit_icon) continue;  // the squadron's icon carries its badge
        const std::u32string label = label_of(u.id);
        Vec2f c;
        if (label.empty() || !unit_icon_centre(f.world, canvas_, u, types[u.type], c)) continue;
        const bool lagging = f.fleet_groups_on && std::any_of(f.fleet_groups.begin(), f.fleet_groups.end(), [&](const HudFleetGroup& g) {
                                 return std::binary_search(g.lagging.begin(), g.lagging.end(), u.id);
                             });
        badge(label, {c.x + 0.5f * f.world.unit_icon_size + 2.0f, c.y}, kWorldLayer, lagging);
        ++stats_.world_badges;
        if (lagging) ++stats_.badges_lagging;
    }
    // On the hero bar's slots.
    for (const HudHeroSlot& s : f.hero_slots) {
        const std::u32string label = label_of(s.hero);
        if (label.empty()) continue;
        const Vec2f lo = canvas_.pixel_to_canvas(s.min_px.x, s.max_px.y), hi = canvas_.pixel_to_canvas(s.max_px.x, s.min_px.y);
        const float h = 1.1f * text_height_units();
        const uint32_t layer = std::min(s.layer + 1, kHudLayers - 1);
        badge(label, {lo.x + 1.0f, hi.y - 0.5f * h - 1.0f}, layer, false);
        ++stats_.hero_badges;
    }
    // On the map: each group's and wing's member centre.
    if (map_view_.valid()) {
        auto centre_of = [&](std::span<const uint32_t> ids, Vec2f& out) {
            float x = 0.0f, y = 0.0f;
            size_t n = 0;
            for (const HudUnit& u : units)
                if (std::binary_search(ids.begin(), ids.end(), u.id)) {
                    x += u.position.x;
                    y += u.position.y;
                    ++n;
                }
            if (n == 0) return false;
            out = map_view_.to_canvas(x / static_cast<float>(n), y / static_cast<float>(n));
            return map_view_.inside(out);
        };
        auto map_badge = [&](std::span<const uint32_t> ids, std::u32string_view text) {
            Vec2f c;
            if (text.empty() || !centre_of(ids, c)) return;
            constexpr bool kNotLagging = false;  // the map badge has no lagging look (#603: the world badge has)
            badge(text, {c.x + 3.0f, c.y}, kControlLayer, kNotLagging);
            ++stats_.radar_badges;
        };
        if (f.fleet_groups_on)
            for (const HudFleetGroup& g : f.fleet_groups) map_badge(g.members, count_text(g.id));
        for (const HudWing& w : f.wings)
            if (w.squadrons > 0) map_badge(w.ids, wing_badge(w));
    }
    // The selected locked groups' outlines.
    if (!f.fleet_groups_on || f.world.targeting_active) return;
    for (const HudFleetGroup& g : f.fleet_groups) {
        if (!g.locked || !std::any_of(units.begin(), units.end(), [&](const HudUnit& u) {
                return u.selected && std::binary_search(g.members.begin(), g.members.end(), u.id);
            }))
            continue;
        const std::optional<FleetOutline> o = fleet_outline(g, units, types);
        if (!o) continue;
        constexpr int kSegments = 32;
        float sx = 0.0f, sy = 0.0f;
        if (!project(f.world.view, o->centre, sx, sy)) continue;
        const Vec2f sc = canvas_.pixel_to_canvas(sx, sy);
        // The ring's points, k = 0..kSegments (the last is the first again, closing it). A point behind the camera is
        // empty, and the edge breaks there rather than jumping across.
        std::vector<std::optional<Vec2f>> ring;
        for (int k = 0; k <= kSegments; ++k) {
            const float a = 6.2831853f * static_cast<float>(k % kSegments) / kSegments;
            const Vec3 p{o->centre.x + o->radius * std::cos(a), o->centre.y + o->radius * std::sin(a), o->centre.z};
            float px = 0.0f, py = 0.0f;
            if (project(f.world.view, p, px, py)) ring.emplace_back(canvas_.pixel_to_canvas(px, py));
            else ring.emplace_back(std::nullopt);
        }
        const Vec2f* top_point = nullptr;
        for (const std::optional<Vec2f>& q : ring)
            if (q && (!top_point || q->y > top_point->y)) top_point = &*q;
        if (!top_point) continue;
        // The dark edge just outside the outline (readable on nebulae and on black space alike).
        auto out = [&](Vec2f q) {
            const float dx = q.x - sc.x, dy = q.y - sc.y, l = std::max(1e-3f, std::sqrt(dx * dx + dy * dy));
            return Vec2f{q.x + 2.5f * dx / l, q.y + 2.5f * dy / l};
        };
        for (size_t k = 1; k < ring.size(); ++k)
            if (ring[k - 1] && ring[k]) line_quad(dl_, kWorldLayer, out(*ring[k - 1]), out(*ring[k]), 3.0f, Rgba8{0, 0, 0, 190});
        // #603 toward each lagging member: an amber dashed section of the ring (0.8 rad, 4 dashes) on the dark edge.
        for (const uint32_t lid : g.lagging) {
            const HudUnit* it = find_unit(units, lid);
            if (!it) continue;
            const float a0 = std::atan2(it->position.y - o->centre.y, it->position.x - o->centre.x);
            constexpr int kSteps = 8;
            constexpr float kHalf = 0.4f;
            auto at = [&](float a, Vec2f& q) {
                const Vec3 p{o->centre.x + o->radius * std::cos(a), o->centre.y + o->radius * std::sin(a), o->centre.z};
                float px = 0.0f, py = 0.0f;
                if (!project(f.world.view, p, px, py)) return false;
                q = out(canvas_.pixel_to_canvas(px, py));
                return true;
            };
            for (int k = 0; k < kSteps; k += 2) {
                Vec2f qa, qb;
                const float a = a0 - kHalf + 2.0f * kHalf * static_cast<float>(k) / kSteps;
                const float b = a0 - kHalf + 2.0f * kHalf * static_cast<float>(k + 1) / kSteps;
                if (at(a, qa) && at(b, qb)) line_quad(dl_, kWorldLayer, qa, qb, 3.0f, colours_.hull_warn);
            }
            ++stats_.outline_lagging;
        }
        // The tag over the outline's top.
        const Vec2f top = *top_point;
        std::u32string text = count_text(g.id);
        if (!g.name.empty()) text += U" " + g.name;
        text += U"  " + text_or_port(text_, nullptr, U"LOCKED");
        HudTextStyle st;
        st.font = font();
        st.color = colours_.text;
        const float th = text_height_units(), h = 1.3f * th, icon = 0.5f * h;
        const float w = measure_text_block(*fonts_, canvas_, text, 0.0f, st).width + 2.0f * kPad + icon;
        // over the top, kept on screen (a group at the top edge has its tag just inside it)
        const Vec2f tc{std::clamp(top.x, -0.5f * canvas_.width + kMargin + 0.5f * w, 0.5f * canvas_.width - kMargin - 0.5f * w),
                       std::clamp(top.y + 4.0f + 0.5f * h, -0.5f * canvas_.height + kMargin + 0.5f * h, 0.5f * canvas_.height - kMargin - 0.5f * h)};
        frame_rect(dl_, kWorldLayer, tc, {w, h}, Rgba8{0, 0, 0, 210}, colours_.hull_warn);
        const float lx = tc.x - 0.5f * w + kPad + 0.5f * icon;
        chip_icon(Chip::Locked, {lx, tc.y}, 0.55f * icon, colours_.hull_warn, kWorldLayer);  // the tabs' padlock
        add_text(dl_, kWorldLayer, HudList::Text, *fonts_, canvas_, text, {lx + 0.5f * icon + 0.5f * kPad, tc.y}, st);
        ++stats_.outline_tags;
    }
}

// #603 the move-preview readout, while a locked move is aimed (the #179 preview slots): over the slots' centre, what a
// locked move does (the formation is kept, the group moves at its slowest ship's pace, the leaders wait for stragglers)
// and, while a move of those groups runs, "N lagging". No ETA (Gabriel, 2026-11-25). No hit box.
void ModernTacticalHud::build_move_readout(const TacticalHudFrame& f, std::span<const HudUnit> units) {
    if (!f.fleet_groups_on || f.fleet_preview.empty() || f.world.targeting_active || f.world.cinematic) return;
    float x = 0.0f, y = 0.0f, z = 0.0f;
    size_t n = 0;
    uint32_t lagging = 0;
    for (const HudFleetGoal& g : f.fleet_preview) {
        const HudUnit* it = find_unit(units, g.id);
        if (!it) continue;
        x += g.x;
        y += g.y;
        z += it->position.z;
        ++n;
    }
    if (n == 0) return;
    for (const HudFleetGroup& g : f.fleet_groups)
        if (std::any_of(f.fleet_preview.begin(), f.fleet_preview.end(),
                        [&](const HudFleetGoal& p) { return std::binary_search(g.members.begin(), g.members.end(), p.id); }))
            lagging += static_cast<uint32_t>(g.lagging.size());
    const float k = 1.0f / static_cast<float>(n);
    float sx = 0.0f, sy = 0.0f;
    if (!project(f.world.view, Vec3{x * k, y * k, z * k}, sx, sy)) return;
    const Vec2f at = canvas_.pixel_to_canvas(sx, sy);
    HudTextStyle st;
    st.font = font();
    st.color = colours_.text;
    HudTextStyle ls = st;
    ls.color = colours_.label;
    const std::u32string lines[3] = {text_or_port(text_, nullptr, U"Locked move: formation kept"),
                                     text_or_port(text_, nullptr, U"Moves at its slowest ship's pace"),
                                     text_or_port(text_, nullptr, U"Leaders wait for stragglers")};
    const float th = text_height_units(), line = 1.3f * th;
    float w = lagging > 0 ? lag_mark_width(lagging, true) : 0.0f;
    for (const std::u32string& l : lines) w = std::max(w, measure_text_block(*fonts_, canvas_, l, 0.0f, st).width);
    w += 2.0f * kPad;
    const float h = line * (lagging > 0 ? 4.0f : 3.0f) + kPad;
    Vec2f c{at.x, at.y + 40.0f + 0.5f * h};
    c.x = std::clamp(c.x, std::max(-0.5f * canvas_.width + kMargin, roster_right_ + kPad) + 0.5f * w, 0.5f * canvas_.width - kMargin - 0.5f * w);
    c.y = std::clamp(c.y, -0.5f * canvas_.height + kMargin + kDeckHeight + 0.5f * h, 0.5f * canvas_.height - kMargin - 0.5f * h);
    frame_rect(dl_, kPanelLayer, c, {w, h}, colours_.back, accent(200));
    const float left = c.x - 0.5f * w + kPad;
    float ly = c.y + 0.5f * h - 0.5f * kPad - 0.5f * line;
    for (int i = 0; i < 3; ++i, ly -= line) add_text(dl_, kPanelLayer, HudList::Text, *fonts_, canvas_, lines[i], {left, ly}, i == 0 ? st : ls);
    if (lagging > 0) lag_mark({left + lag_mark_width(lagging, true), ly}, lagging, true, kPanelLayer);
    stats_.move_readout = true;
    stats_.move_readout_lagging = lagging;
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

// The order buttons (2 rows of 3, the right end of the deck); the fleet discipline control beside them (#599) shows the
// cohesion Classic writes as a line.
float ModernTacticalHud::build_orders(const TacticalHudFrame& f, Vec2f top_right, float height) {
    const float line = text_height_units();
    const float row_h = 0.5f * (height - 0.5f * kPad);
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
    return width;
}

// #599 the fleet discipline control (space battles, where cohesion applies): Loose / Tight / Locked (fleet_discipline)
// as position-coded lights with labels, the lit one filled; a click takes the next step. Under it New group (N) and
// Lock (Y) while fleet groups are on. Hovered, a hint names each step in one line. Returns the width (0: not drawn).
float ModernTacticalHud::build_discipline(const TacticalHudFrame& f, std::span<const HudUnit> units, Vec2f top_right, float height) {
    discipline_drawn_ = false;
    if (f.cohesion_text.empty()) return 0.0f;  // a land battle (no cohesion) or no feed
    std::vector<uint32_t> selection;
    for (const HudUnit& u : units)
        if (u.selected && u.owner == f.world.local_player) selection.push_back(u.id);
    discipline_ = fleet_discipline(f.fleet_groups_on, f.fleet_groups, selection, f.cohesion_tight);
    discipline_selection_ = !selection.empty();
    const float th = text_height_units(), gap = 0.5f * kPad;
    const uint32_t steps = f.fleet_groups_on ? 3u : 2u;
    const float width = 10.0f * th;
    const float button_h = f.fleet_groups_on ? std::max(0.4f * height, 1.6f * th) : 0.0f;
    const float control_h = height - (button_h > 0.0f ? button_h + gap : 0.0f);
    const Vec2f c{top_right.x - 0.5f * width, top_right.y - 0.5f * control_h};
    frame_rect(dl_, kControlLayer, c, {width, control_h}, colours_.button, hover_ == kModernDisciplineHit ? colours_.text : accent(200));
    hit(kModernDisciplineHit, kControlLayer, c, {width, control_h});
    static constexpr const char32_t* kStepLabels[3] = {U"Loose", U"Tight", U"Locked"};
    HudTextStyle ls;
    ls.font = font();
    ls.align = TextAlign::Center;
    const float slot = width / static_cast<float>(steps);
    const float light = std::min(0.8f * th, 0.3f * control_h);
    const float light_y = c.y + 0.5f * control_h - kPad - 0.5f * light;
    for (uint32_t k = 0; k < steps; ++k) {  // left to right: the position names the step, as well as the label
        const float x = c.x - 0.5f * width + (static_cast<float>(k) + 0.5f) * slot;
        const bool lit = static_cast<uint32_t>(discipline_.shown) == k;
        if (lit) frame_rect(dl_, kControlLayer, {x, light_y}, {light, light}, accent(255), colours_.text);
        else frame_rect(dl_, kControlLayer, {x, light_y}, {light, light}, Rgba8{0, 0, 0, 170}, colours_.label);
        const std::u32string label = text_or_port(text_, nullptr, kStepLabels[k]);
        ls.color = lit ? colours_.text : colours_.label;
        ls.width_scale = fit_width_scale(measure_text_block(*fonts_, canvas_, label, 0.0f, ls).width, slot - 0.5f * kPad);
        add_text(dl_, kControlLayer, HudList::Text, *fonts_, canvas_, label, {x, light_y - 0.5f * light - 0.8f * th}, ls);
    }
    key_badge(hk::kFleetCohesion, {c.x + 0.5f * width - 2.0f, c.y - 0.5f * control_h + 1.2f * th}, kControlLayer);  // O: Loose / Tight
    discipline_drawn_ = true;
    stats_.discipline_drawn = true;
    stats_.discipline_lights = steps;
    stats_.discipline = discipline_.shown;
    if (f.fleet_groups_on) {  // New group (N) and Lock (Y), the keys' orders
        const float bw = 0.5f * (width - gap);
        const float by = top_right.y - control_h - gap - 0.5f * button_h;
        for (const auto& [id, label, slot_key] : {std::tuple{kModernNewGroupHit, U"New group", hk::kFleetCreate},
                                                  std::tuple{kModernLockHit, discipline_.selection_locked ? U"Unlock" : U"Lock", hk::kFleetLock}}) {
            Tile t;
            t.id = id;
            t.size = {bw, button_h};
            t.c = {top_right.x - width + (id == kModernNewGroupHit ? 0.5f * bw : 1.5f * bw + gap), by};
            t.text = text_or_port(text_, nullptr, label);
            t.hotkey = slot_key;
            tile(t);
            ++stats_.fleet_buttons;
        }
    }
    if (hover_ == kModernDisciplineHit) {  // the hint: one line per step, above the control
        static constexpr const char32_t* kHints[3] = {U"Loose: each ship flies at its own speed",
                                                      U"Tight: ships keep to the slowest ship's speed",
                                                      U"Locked: the group keeps formation and waits for stragglers"};
        HudTextStyle hs;
        hs.font = font();
        hs.color = colours_.text;
        float hw = 0.0f;
        std::vector<std::u32string> lines;
        for (uint32_t k = 0; k < steps; ++k) {
            lines.push_back(text_or_port(text_, nullptr, kHints[k]));
            hw = std::max(hw, measure_text_block(*fonts_, canvas_, lines.back(), 0.0f, hs).width);
        }
        const float line = 1.4f * th;
        const Vec2f size{hw + 2.0f * kPad, line * static_cast<float>(steps) + kPad};
        const float right = std::min(top_right.x, 0.5f * canvas_.width - kMargin);
        const Vec2f hc{right - 0.5f * size.x, top_right.y + kPad + 0.5f * size.y};
        frame_rect(dl_, kBannerLayer, hc, size, colours_.back, accent(220));
        float y = hc.y + 0.5f * size.y - 0.5f * kPad - 0.5f * line;
        for (const std::u32string& l : lines) {
            add_text(dl_, kBannerLayer, HudList::Text, *fonts_, canvas_, l, {hc.x - 0.5f * size.x + kPad, y}, hs);
            y -= line;
        }
        stats_.discipline_hint = true;
    }
    return width;
}

// #601 the fleet roster at the left edge, under the status corner and above the deck: a card per squadron wing (its
// craft's icon, name, key, squadrons and craft left, health, a WING chip), then with fleet groups on a card per group in
// creation order (its first member's icon, number and name, ships, health, a LOCKED / FREE chip) and "+ New group (N)".
// Cards with units in the selection get a border and a side bar. The header collapses the roster to itself.
void ModernTacticalHud::build_roster(const TacticalHudFrame& f, std::span<const HudUnit> units, std::span<const HudTypeInfo> types) {
    struct Card {
        int32_t id;
        std::u32string name, detail, chip;
        std::string icon;
        float health;
        bool selected;
        Chip state;
        uint32_t lagging = 0;  // #603
    };
    auto icon_of = [&](std::span<const uint32_t> ids) {
        for (const HudUnit& u : units)
            if (!ids.empty() && std::binary_search(ids.begin(), ids.end(), u.id) && u.type < types.size()) return types[u.type].icon_name;
        return std::string();
    };
    auto any_selected = [&](std::span<const uint32_t> ids) {
        return std::any_of(units.begin(), units.end(), [&](const HudUnit& u) { return u.selected && std::binary_search(ids.begin(), ids.end(), u.id); });
    };
    std::vector<Card> cards;
    for (size_t w = 0; w < f.wings.size() && w < kMaxWings; ++w) {
        const HudWing& wing = f.wings[w];
        if (wing.squadrons == 0 || wing.ids.empty()) continue;
        std::u32string name = wing.label;
        if (!wing.hotkey.empty()) name += U" " + wing.hotkey;  // always: what one key press selects (#596 story 2)
        std::u32string detail = count_text(wing.squadrons) + U" " + text_or_port(text_, nullptr, wing.squadrons == 1 ? U"squadron" : U"squadrons");
        if (wing.craft_total > 0) detail += U", " + count_text(wing.craft_alive) + U"/" + count_text(wing.craft_total) + U" " + text_or_port(text_, nullptr, U"craft");
        cards.push_back({kWingHitBase + static_cast<int32_t>(w), std::move(name), std::move(detail), text_or_port(text_, nullptr, U"WING"),
                         icon_of(wing.ids), wing.health, any_selected(wing.ids), Chip::Wing, 0});
    }
    std::vector<uint32_t> own_selected;
    for (const HudUnit& u : units)
        if (u.selected && u.owner == f.world.local_player) own_selected.push_back(u.id);
    if (f.fleet_groups_on)
        for (const HudFleetGroup& g : f.fleet_groups) {
            if (g.members.empty()) continue;
            const auto k = static_cast<int32_t>(roster_groups_.size());
            std::u32string name = count_text(g.id);
            if (!g.name.empty()) name += U" " + g.name;
            std::u32string detail = count_text(static_cast<uint32_t>(g.members.size())) + U" " +
                                    text_or_port(text_, nullptr, g.members.size() == 1 ? U"ship" : U"ships");
            cards.push_back({kModernRosterGroupBase + k, std::move(name), std::move(detail), text_or_port(text_, nullptr, g.locked ? U"LOCKED" : U"FREE"),
                             icon_of(g.members), g.health, any_selected(g.members), g.locked ? Chip::Locked : Chip::Free,
                             static_cast<uint32_t>(g.lagging.size())});
            RosterGroup rg;
            rg.id = g.id;
            rg.name = utf32_to_utf8(g.name);
            rg.members = g.members;
            Vec2f sum{};
            size_t n = 0;
            for (const HudUnit& u : units)
                if (std::binary_search(g.members.begin(), g.members.end(), u.id)) {
                    sum = {sum.x + u.position.x, sum.y + u.position.y};
                    ++n;
                }
            if (n > 0) rg.centre = {sum.x / static_cast<float>(n), sum.y / static_cast<float>(n)};
            roster_groups_.push_back(std::move(rg));
        }
    if (cards.empty() && !f.fleet_groups_on) return;
    const float th = text_height_units(), gap = 0.5f * kPad;
    const float width = std::min(0.2f * canvas_.width, 15.0f * th);
    const float left = -0.5f * canvas_.width + kMargin;
    const float top = status_bottom_ - kPad;
    // Above the deck, and above the map when it sits at the deck's left end (#591).
    float bottom = -0.5f * canvas_.height + kMargin + kDeckHeight + kPad;
    if (map_view_.valid() && !options_.map_right) bottom = std::max(bottom, map_view_.center.y + 0.5f * map_view_.size.y + kPad);
    const float header_h = std::max(1.6f * th, px_units(24.0f));
    if (top - bottom < header_h) return;
    HudTextStyle st;
    st.font = font();
    st.color = colours_.text;
    HudTextStyle ls = st;
    ls.color = colours_.label;
    // Header: "Fleet" and the collapse mark (- open, + collapsed).
    const Vec2f hc{left + 0.5f * width, top - 0.5f * header_h};
    frame_rect(dl_, kPanelLayer, hc, {width, header_h}, colours_.back, hover_ == kModernRosterHeaderHit ? colours_.text : accent(200));
    add_text(dl_, kPanelLayer, HudList::Text, *fonts_, canvas_, text_or_port(text_, nullptr, U"Fleet"), {left + kPad, hc.y}, st);
    HudTextStyle ms = st;
    ms.align = TextAlign::Right;
    add_text(dl_, kPanelLayer, HudList::Text, *fonts_, canvas_, roster_collapsed_ ? U"+" : U"-", {left + width - kPad, hc.y}, ms);
    hit(kModernRosterHeaderHit, kPanelLayer, hc, {width, header_h});
    stats_.roster = true;
    stats_.roster_collapsed = roster_collapsed_;
    roster_right_ = left + width;  // #603 the move readout keeps to its right
    if (roster_collapsed_) return;
    // Every card when they fit; else the cards shrink to one line (no detail line), and what still does not fit is counted
    // in a "+N more" line (the roster always ends with "+ New group" while fleet groups are on).
    const float new_h = f.fleet_groups_on ? std::max(1.8f * th, px_units(24.0f)) + gap : 0.0f;
    const float more_h = 1.4f * th + gap;
    const float avail = top - header_h - gap - bottom - new_h;
    const float full_h = std::max(2.8f * th, px_units(24.0f)), slim_h = std::max(1.6f * th, px_units(24.0f));
    const auto n_cards = static_cast<float>(cards.size());
    float card_h = full_h;
    size_t shown = cards.size();
    if (n_cards * (full_h + gap) > avail) {
        card_h = std::max(slim_h, avail / std::max(1.0f, n_cards) - gap);
        if (n_cards * (card_h + gap) > avail) shown = static_cast<size_t>(std::max(0.0f, std::floor((avail - more_h) / (card_h + gap))));
    }
    const bool detail = card_h >= 2.6f * th;
    float y = top - header_h - gap;
    for (size_t ci = 0; ci < shown && ci < cards.size(); ++ci) {
        const Card& c = cards[ci];
        const Vec2f cc{hc.x, y - 0.5f * card_h};
        const bool locked = c.state == Chip::Locked;
        frame_rect(dl_, kControlLayer, cc, {width, card_h}, colours_.button,
                   hover_ == c.id || c.selected ? colours_.text : locked ? colours_.hull_warn : accent(200));
        if (c.selected) {  // the selection: a side bar as well as the border
            add_rect(dl_, kControlLayer, left, cc.y - 0.5f * card_h, left + 4.0f, cc.y + 0.5f * card_h, colours_.text);
            ++stats_.roster_selected;
        }
        const float portrait = card_h - 2.0f * gap;
        float x = left + 4.0f + gap;
        if (!c.icon.empty() && atlas_)
            add_atlas_quad(dl_, kControlLayer, HudList::Alpha, *atlas_, c.icon, {x + 0.5f * portrait, cc.y}, {portrait, portrait}, 1.0f, Rgba8{255, 255, 255, 255});
        x += portrait + gap;
        const float right = left + width - gap;
        // The chip (top right): its icon and text on a dark plate; locked also as the border's warning colour.
        HudTextStyle cs = st;
        cs.align = TextAlign::Center;
        const Rgba8 chip_edge = locked ? colours_.hull_warn : accent(220);
        const float icon_u = 0.22f * 1.1f * th * 2.0f;
        float chip_w = measure_text_block(*fonts_, canvas_, c.chip, 0.0f, cs).width + kPad + 2.0f * icon_u;
        // #603 "N lagging": at the detail line's end, or (a slim card) before the chip. A slim card short of room keeps
        // the chip's icon only; the words give way to the triangle and N when the line has no room for both.
        const float lag_short = c.lagging > 0 ? lag_mark_width(c.lagging, false) : 0.0f;
        bool chip_text = true;
        if (c.lagging > 0 && !detail && right - chip_w - kPad - x < lag_short + kPad + th) {
            chip_text = false;
            chip_w = kPad + 2.0f * icon_u;
        }
        const float line_y = detail ? cc.y + 0.5f * card_h - gap - 0.5f * th : cc.y;
        const Vec2f chip_c{right - 0.5f * chip_w, line_y};
        frame_rect(dl_, kControlLayer, chip_c, {chip_w, 1.1f * th}, Rgba8{0, 0, 0, 200}, chip_edge);
        chip_icon(c.state, {chip_c.x - 0.5f * chip_w + 0.5f * kPad + 0.6f * icon_u, chip_c.y}, icon_u, chip_edge, kControlLayer);
        if (chip_text) add_text(dl_, kControlLayer, HudList::Text, *fonts_, canvas_, c.chip, {chip_c.x + icon_u, chip_c.y}, cs);
        float name_right = right - chip_w - kPad, detail_right = right;
        if (c.lagging > 0) {
            float& edge = detail ? detail_right : name_right;
            const bool words = edge - x >= lag_mark_width(c.lagging, true) + 2.0f * th;
            const Vec2f at{std::max(edge, x + lag_short), detail ? line_y - 1.2f * th : line_y};
            edge = at.x - lag_mark(at, c.lagging, words, kControlLayer) - kPad;
            ++stats_.roster_lagging;
        }
        HudTextStyle ns = st;
        ns.width_scale = fit_width_scale(measure_text_block(*fonts_, canvas_, c.name, 0.0f, ns).width, std::max(1.0f, name_right - x));
        add_text(dl_, kControlLayer, HudList::Text, *fonts_, canvas_, c.name, {x, line_y}, ns);
        if (detail) {
            HudTextStyle ds = ls;
            ds.width_scale = fit_width_scale(measure_text_block(*fonts_, canvas_, c.detail, 0.0f, ds).width, std::max(1.0f, detail_right - x));
            add_text(dl_, kControlLayer, HudList::Text, *fonts_, canvas_, c.detail, {x, line_y - 1.2f * th}, ds);
        }
        if (c.health >= 0.0f) add_bar(dl_, kControlLayer, {0.5f * (x + right), cc.y - 0.5f * card_h + gap + 2.0f}, {right - x, 4.0f}, c.health, hull_colour(c.health), false);
        hit(c.id, kControlLayer, cc, {width, card_h});
        if (c.state == Chip::Wing) ++stats_.wings;
        else ++stats_.roster_groups;
        y -= card_h + gap;
    }
    if (shown < cards.size()) {  // "+N more": the cards left out
        HudTextStyle mo = ls;
        add_text(dl_, kControlLayer, HudList::Text, *fonts_, canvas_,
                 replace_all(text_or_port(text_, nullptr, U"+##0 more"), U"##0", count_text(static_cast<uint32_t>(cards.size() - shown))),
                 {left + kPad, y - 0.5f * more_h}, mo);
        stats_.roster_more = static_cast<uint32_t>(cards.size() - shown);
        y -= more_h;
    }
    auto room = [&](float h) { return y - h >= bottom - 0.5f; };
    if (f.fleet_groups_on && room(new_h - gap)) {  // "+ New group (N)": N's Create of the selection
        roster_new_live_ = !own_selected.empty();
        Tile t;
        t.id = kModernRosterNewHit;
        t.size = {width, std::max(1.8f * th, px_units(24.0f))};
        t.c = {hc.x, y - 0.5f * t.size.y};
        t.text = text_or_port(text_, nullptr, U"+ New group");
        t.hotkey = hk::kFleetCreate;
        t.enabled = roster_new_live_;
        tile(t);
        stats_.roster_new = true;
    }
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

void ModernTacticalHud::update_input(const RoutedInput& routed, bool shift, bool ctrl, std::vector<commands::EventData>& out,
                                     std::vector<HudAction>* actions) {
    hover_ = routed.hover_component;
    if (!table_) return;
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
        if (view_click(e, actions)) continue;  // the style switch, Jump
        // Cards: the Classic portrait click (TacticalHud::update_input "Selection portraits").
        if (e.component >= kModernCardHitBase && e.component - kModernCardHitBase < static_cast<int32_t>(groups_.size())) {
            if (e.button != Button::Left) continue;
            const SelectedGroup& g = groups_[static_cast<size_t>(e.component - kModernCardHitBase)];
            out.push_back(commands::ev::select(portrait_select_ids(g, selection_ids(), shift, ctrl)));
            continue;
        }
        // #599 the discipline control's step (fleet_discipline: SetLock and / or the cohesion), New group (N's Create) and
        // Lock (Y's SetLock of the selection).
        if (e.component == kModernDisciplineHit) {
            if (e.button == Button::Left && discipline_drawn_) {
                if (discipline_.lock) out.push_back(fleet_lock_event(*discipline_.lock));
                if (discipline_.cohesion) {
                    HudAction h;
                    h.kind = HudAction::Kind::FleetCohesion;
                    h.component = e.component;
                    h.value = *discipline_.cohesion ? 1 : 0;
                    act(std::move(h));
                }
            }
            continue;
        }
        if (e.component == kModernNewGroupHit || e.component == kModernLockHit) {
            if (e.button == Button::Left && discipline_drawn_ && discipline_selection_)
                out.push_back(e.component == kModernLockHit ? fleet_lock_event(!discipline_.selection_locked) : fleet_button_event(false));
            continue;
        }
        if (e.component == kModernClearHit) {
            if (e.button == Button::Left) out.push_back(commands::ev::select({}));
            continue;
        }
        // #601 the roster: the header collapses it; a group card is Classic's group tab click (Shift adds; a second left
        // click within 0.5 s also centres the camera on the group), right renames it; "+ New group" is N's Create.
        if (e.component == kModernRosterHeaderHit) {
            if (e.button == Button::Left) roster_collapsed_ = !roster_collapsed_;
            continue;
        }
        if (e.component == kModernRosterNewHit) {
            if (e.button == Button::Left && roster_new_live_) out.push_back(fleet_button_event(false));
            continue;
        }
        if (e.component >= kModernRosterGroupBase && e.component - kModernRosterGroupBase < static_cast<int32_t>(roster_groups_.size())) {
            const RosterGroup& g = roster_groups_[static_cast<size_t>(e.component - kModernRosterGroupBase)];
            if (e.button == Button::Right) {
                act(fleet_rename_action(e.component, g.id, g.name));
            } else if (e.button == Button::Left) {
                out.push_back(commands::ev::select(fleet_tab_select_ids(g.members, selection_ids(), shift)));
                constexpr double kDoubleClick = 0.5;  // the hero bar's (InputConfig::double_click_time)
                const bool dbl = last_roster_click_.group == g.id && time_ - last_roster_click_.time <= kDoubleClick;
                last_roster_click_ = dbl ? RosterClick{} : RosterClick{g.id, time_};
                if (dbl) act(radar_jump_action(e.component, g.centre));
            }
            continue;
        }
        if (e.component == kModernStationHit) {
            if (e.button == Button::Left && station_ != commands::kNoObject) out.push_back(commands::ev::select({station_}));
            continue;
        }
        // #600 a wing run's tab: the Classic wing button's action.
        if (e.component >= kModernWingTabBase && e.component - kModernWingTabBase < static_cast<int32_t>(wing_ids_.size())) {
            const auto w = static_cast<size_t>(e.component - kModernWingTabBase);
            if (e.button == Button::Left && !wing_ids_[w].empty()) act(wing_select_action(static_cast<int32_t>(w), wing_ids_[w], shift, ctrl));
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
        act(button_action(e, shift, ctrl));
    }
}

}  // namespace alamo::ui
