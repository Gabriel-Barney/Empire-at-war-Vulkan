// alamo_ui_hud_modern_tests (#584, docs/DECISIONS.md "Modern HUD: exception to the current focus"): the Modern tactical
// HUD style against the Classic one on the HUD seam (frame in; stats, hit boxes, commands and actions out): the status
// corner shows the Classic values, the shared game controls send the Classic commands and actions, the style switch
// asks the host to switch, the style names, the text scale, and the Classic world-space split.
#include <cstdio>
#include <memory>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

#include "alamo/data/xml.hpp"
#include "alamo/test/support.hpp"
#include "alamo/ui/hotkeys.hpp"
#include "alamo/ui/hud_font.hpp"
#include "alamo/ui/modern_tactical_hud.hpp"
#include "alamo/ui/orders.hpp"
#include "alamo/ui/tactical_hud.hpp"
#include "hud_test_data.hpp"

using namespace alamo;
using namespace alamo::ui;
using ::alamo::test::g_fail;

namespace {

const char* kXml = R"(<?xml version="1.0"?>
<CommandBarComponents>
  <CommandBarComponent Name="b_option_t"><Type>TextButton</Type><Tooltip_Text>TEXT_BUTTON_MAIN_MENU</Tooltip_Text></CommandBarComponent>
  <CommandBarComponent Name="b_play_pause_t"><Type>Button</Type></CommandBarComponent>
  <CommandBarComponent Name="b_fast_forward_t"><Type>Button</Type></CommandBarComponent>
  <CommandBarComponent Name="text_attack"><Type>TextButton</Type></CommandBarComponent>
  <CommandBarComponent Name="attack_button"><Type>TextButton</Type></CommandBarComponent>
  <CommandBarComponent Name="Text_Credits_tactical"><Type>TextButton</Type>
    <Font_Name>EmpireAtWar-Medium</Font_Name><Font_Point_Size>7</Font_Point_Size></CommandBarComponent>
  <CommandBarComponent Name="Text_Tactical_Tech"><Type>TextButton</Type></CommandBarComponent>
  <CommandBarComponent Name="Text_Planetary_Pop"><Type>TextButton</Type></CommandBarComponent>
  <CommandBarComponent Name="s_select_00"><Type>TextButton</Type><Base_Layer>4</Base_Layer></CommandBarComponent>
  <CommandBarComponent Name="s_select_01"><Type>TextButton</Type><Base_Layer>4</Base_Layer></CommandBarComponent>
  <CommandBarComponent Name="s_select_02"><Type>TextButton</Type><Base_Layer>4</Base_Layer></CommandBarComponent>
  <CommandBarComponent Name="c_button00"><Type>Button</Type></CommandBarComponent>
  <CommandBarComponent Name="c_button01"><Type>Button</Type></CommandBarComponent>
  <CommandBarComponent Name="c_button02"><Type>Button</Type></CommandBarComponent>
  <CommandBarComponent Name="c_button03"><Type>Button</Type></CommandBarComponent>
  <CommandBarComponent Name="c_button04"><Type>Button</Type></CommandBarComponent>
  <CommandBarComponent Name="c_button05"><Type>Button</Type></CommandBarComponent>
</CommandBarComponents>)";

// Two own groups (types 0 and 1, type 1 shielded) and an enemy unit, all selected but the enemy.
struct Selection {
    std::vector<HudTypeInfo> types;
    std::vector<HudUnit> units;
    Selection() {
        types.resize(2);
        types[0].name = "Rebel_X_Wing";
        types[0].icon_name = "i_icon_unit.tga";
        types[1].name = "Corellian_Corvette";
        types[1].has_shields = true;
        auto add = [&](uint32_t id, uint32_t type, int32_t owner, bool selected, float health, float shield) {
            HudUnit u;
            u.id = id;
            u.type = type;
            u.owner = owner;
            u.selected = selected;
            u.health = health;
            u.shield = shield;
            units.push_back(u);
        };
        add(20, 0, 0, true, 1.0f, -1.0f);
        add(21, 0, 0, true, 0.2f, -1.0f);
        add(22, 1, 0, true, 0.5f, 0.75f);
        add(30, 1, 1, false, 1.0f, 1.0f);
    }
};

ComponentTable table() {
    ComponentTable t;
    const data::XmlDocument doc = data::XmlDocument::parse(std::string_view(kXml), "modern.xml");
    if (doc.ok()) t.parse(doc.root());
    return t;
}

TacticalHudFrame frame() {
    TacticalHudFrame f;
    f.credits = 13000;
    f.pop = 13;
    f.pop_cap = 20;
    f.tech = 3;
    f.time = 1.0;
    return f;
}

RoutedInput click_on(int32_t component, Button b = Button::Left) {
    RoutedInput r;
    r.hover_component = component;
    r.hud = {{HudPointerEvent::Kind::Press, b, component, 0, 0}, {HudPointerEvent::Kind::Release, b, component, 0, 0},
             {HudPointerEvent::Kind::Click, b, component, 0, 0}};
    return r;
}

bool same_action(const HudAction& a, const HudAction& b) {
    return a.kind == b.kind && a.component == b.component && a.name == b.name && a.button == b.button && a.shift == b.shift &&
           a.ctrl == b.ctrl && a.value == b.value && a.unit == b.unit && a.ids == b.ids;
}

// The centre pixel of the hit box with `id` (false: none).
bool centre_px(const std::vector<HudHitBox>& hits, const HudCanvas& c, int32_t id, int32_t& px, int32_t& py) {
    for (const HudHitBox& h : hits)
        if (h.component == id) {
            const Vec2f p = c.canvas_to_pixel(0.5f * (h.min.x + h.max.x), 0.5f * (h.min.y + h.max.y));
            px = static_cast<int32_t>(p.x);
            py = static_cast<int32_t>(p.y);
            return true;
        }
    return false;
}

struct Huds {
    ComponentTable t = table();
    TextureAtlas atlas = hud_test::make_atlas();
    std::unique_ptr<GlyphRasterizer> raster = make_box_rasterizer();
    FontCache fonts{*raster};
    TacticalHud classic;
    ModernTacticalHud modern;
    Huds() {
        classic.init(t, atlas, fonts);
        classic.resize(1920, 1080);
        modern.init(t, atlas, fonts);
        modern.resize(1920, 1080);
    }
    void build(const TacticalHudFrame& f, std::span<const HudUnit> units = {}, std::span<const HudTypeInfo> types = {}) {
        classic.build(f, units, types);
        modern.build(f, units, types);
    }
    // Both HUDs' output for one click: Classic on `classic_id`, Modern on `modern_id`.
    bool same_click(int32_t classic_id, int32_t modern_id, bool shift, bool ctrl, Button b = Button::Left,
                    size_t* emitted = nullptr) {
        std::vector<commands::EventData> cc, mc;
        std::vector<HudAction> ca, ma;
        classic.update_input(click_on(classic_id, b), shift, ctrl, 1.0, cc, &ca);
        modern.update_input(click_on(modern_id, b), shift, ctrl, mc, &ma);
        bool same = cc == mc && ca.size() == ma.size();
        for (size_t i = 0; same && i < ca.size(); ++i) same = same_action(ca[i], ma[i]);
        if (emitted) *emitted = cc.size() + ca.size();
        if (!same)
            std::printf("  click %d / %d (shift %d ctrl %d): classic %zu cmds %zu acts, modern %zu cmds %zu acts\n", classic_id,
                        modern_id, shift, ctrl, cc.size(), ca.size(), mc.size(), ma.size());
        return same;
    }
};

void test_deck() {
    Huds h;
    Selection s;
    TacticalHudFrame f = frame();
    f.world.local_player = 0;
    f.order_buttons.can_attack = true;  // attack, attack-move and guard live; move, waypoint and stop not
    HudWing wing;
    wing.label = U"FIGHTERS";
    wing.squadrons = 2;
    wing.ids = {40, 41};
    std::vector<HudWing> wings{wing};
    f.wings = wings;
    h.build(f, s.units, s.types);
    const ModernHudStats& st = h.modern.stats();
    // Cards: the Classic grouping, the enemy left out as Classic leaves it.
    CHECK(st.cards == 2 && !st.overflow_card && st.wings == 1 && st.clear_button);
    CHECK(h.modern.selection_cards().size() == h.classic.panel_groups().size());
    for (size_t i = 0; i < h.modern.selection_cards().size() && i < h.classic.panel_groups().size(); ++i)
        CHECK(h.modern.selection_cards()[i].ids == h.classic.panel_groups()[i].ids && h.modern.selection_cards()[i].type == h.classic.panel_groups()[i].type);
    // Orders: the shared rule, the same liveness as Classic.
    CHECK(st.orders == 6 && st.orders_live == 3);
    for (uint32_t i = 0; i < 6; ++i) CHECK(h.modern.order_button_live(i) == h.classic.order_button_live(i));
    // Card clicks (plain, Shift, Ctrl) = the Classic portrait clicks.
    for (int32_t card = 0; card < 2; ++card)
        for (const auto& [shift, ctrl] : {std::pair{false, false}, std::pair{true, false}, std::pair{false, true}}) {
            h.build(f, s.units, s.types);
            size_t n = 0;
            CHECK(h.same_click(h.t.find(card == 0 ? "s_select_00" : "s_select_01"), kModernCardHitBase + card, shift, ctrl,
                               Button::Left, &n) &&
                  n == 1);
        }
    // Orders: a live one sends the Classic Button action, a dead one nothing in either style.
    for (const char* name : {"c_button00", "c_button02", "c_button05"}) {
        h.build(f, s.units, s.types);
        size_t n = 0;
        CHECK(h.same_click(h.t.find(name), h.t.find(name), false, false, Button::Left, &n));
        CHECK(n == (std::string_view(name) == "c_button02" ? 0u : 1u));
    }
    // The wing button: Classic's id and WingSelect.
    h.build(f, s.units, s.types);
    size_t n = 0;
    CHECK(h.same_click(kWingHitBase, kWingHitBase, true, false, Button::Left, &n) && n == 1);
    // Clear: an empty selection.
    std::vector<commands::EventData> cmds;
    h.modern.update_input(click_on(kModernClearHit), false, false, cmds, nullptr);
    CHECK(cmds.size() == 1 && cmds[0] == commands::ev::select({}));
    // Key badges show the live bindings when the host gives them.
    CHECK(st.key_badges == 0);
    HotkeyTable keys;
    h.modern.set_hotkeys(&keys);
    h.modern.build(f, s.units, s.types);
    CHECK(h.modern.stats().key_badges >= 5);
    // Nothing own selected: no cards, no orders (and the order hotkeys are not live).
    std::vector<HudUnit> none = s.units;
    for (HudUnit& u : none) u.selected = false;
    h.build(f, none, s.types);
    CHECK(h.modern.stats().cards == 0 && h.modern.stats().orders == 0 && !h.modern.order_button_live(0));
    // More groups than fit: the last card is "+N" with every unit not shown, and no unit is lost.
    std::vector<HudTypeInfo> many(60);
    std::vector<HudUnit> crowd;
    for (uint32_t i = 0; i < 60; ++i) {
        HudUnit u;
        u.id = 100 + i;
        u.type = i;
        u.owner = 0;
        u.selected = true;
        crowd.push_back(u);
    }
    h.modern.build(f, crowd, many);
    size_t total = 0;
    for (const SelectedGroup& g : h.modern.selection_cards()) total += g.ids.size();
    size_t grouped = 0;  // the Classic grouping keeps its first 48 groups
    for (const SelectedGroup& g : group_selection(crowd)) grouped += g.ids.size();
    CHECK(grouped == 48 && h.modern.stats().overflow_card && h.modern.selection_cards().back().overflow && total == grouped &&
          h.modern.selection_cards().size() < 48);
    // The "+N" card selects every unit it stands for, as the Classic "+N" portrait does.
    const SelectedGroup rest = h.modern.selection_cards().back();
    cmds.clear();
    h.modern.update_input(click_on(kModernCardHitBase + static_cast<int32_t>(h.modern.selection_cards().size() - 1)), false, false,
                          cmds, nullptr);
    CHECK(cmds.size() == 1 && cmds[0] == commands::ev::select(rest.ids));
    // Fleet groups on: the Classic fleet grouping, and the same card clicks.
    std::vector<HudFleetGroup> fleets(1);
    fleets[0].id = 1;
    fleets[0].members = {21, 22};
    f.fleet_groups_on = true;
    f.fleet_groups = fleets;
    h.build(f, s.units, s.types);
    CHECK(h.modern.selection_cards().size() == h.classic.panel_groups().size());
    for (size_t i = 0; i < h.modern.selection_cards().size() && i < h.classic.panel_groups().size(); ++i)
        CHECK(h.modern.selection_cards()[i].ids == h.classic.panel_groups()[i].ids &&
              h.modern.selection_cards()[i].fleet == h.classic.panel_groups()[i].fleet);
    for (int32_t card = 0; card < 2; ++card) {
        h.build(f, s.units, s.types);
        CHECK(h.same_click(h.t.find(card == 0 ? "s_select_00" : "s_select_01"), kModernCardHitBase + card, false, false));
    }
}

void test_style_names() {
    CHECK(hud_style_name(HudStyle::Classic) == "classic" && hud_style_name(HudStyle::Modern) == "modern");
    CHECK(parse_hud_style("classic") == HudStyle::Classic && parse_hud_style("Modern") == HudStyle::Modern);
    CHECK(!parse_hud_style("") && !parse_hud_style("retro"));
    CHECK(other_hud_style(HudStyle::Classic) == HudStyle::Modern && other_hud_style(HudStyle::Modern) == HudStyle::Classic);
}

void test_status_corner() {
    Huds h;
    const TacticalHudFrame f = frame();
    h.build(f);
    const ModernHudStats& s = h.modern.stats();
    // The Classic HUD's texts for the same frame: the credits text it draws, the tech and population components' texts.
    CHECK(s.status_corner);
    CHECK(s.credits_text == hud_credits_text(13000) && s.credits_text == U"$ 13,000");
    CHECK(s.tech_text == h.classic.buttons().state(static_cast<uint32_t>(h.t.find("Text_Tactical_Tech"))).text);
    CHECK(s.pop_text == h.classic.buttons().state(static_cast<uint32_t>(h.t.find("Text_Planetary_Pop"))).text);
    CHECK(s.tech_text == U"TEXT_TECH 3" && s.pop_text == U"13/20");
    // No cap: the used count alone, as Classic.
    TacticalHudFrame g = f;
    g.pop_cap = -1;
    h.build(g);
    CHECK(h.modern.stats().pop_text == U"13" &&
          h.modern.stats().pop_text == h.classic.buttons().state(static_cast<uint32_t>(h.t.find("Text_Planetary_Pop"))).text);
    // Nothing to show: no corner.
    TacticalHudFrame e;
    h.build(e);
    CHECK(!h.modern.stats().status_corner && h.modern.stats().credits_text.empty() && h.modern.stats().pop_text.empty() &&
          h.modern.stats().tech_text.empty());
    CHECK(h.modern.stats().deck && h.modern.stats().style_switch && h.modern.stats().controls == 3);
}

void test_controls_parity() {
    Huds h;
    for (const bool paused : {false, true}) {
        TacticalHudFrame f = frame();
        f.paused = paused;
        for (const char* name : {"b_option_t", "b_play_pause_t", "b_fast_forward_t"}) {
            for (const bool shift : {false, true})
                for (const Button b : {Button::Left, Button::Right}) {
                    h.build(f);
                    const int32_t ci = h.t.find(name);
                    std::vector<commands::EventData> cc, mc;
                    std::vector<HudAction> ca, ma;
                    h.classic.update_input(click_on(ci, b), shift, false, 1.0, cc, &ca);
                    h.modern.update_input(click_on(ci, b), shift, false, mc, &ma);
                    bool same = cc == mc && ca.size() == ma.size();
                    for (size_t i = 0; same && i < ca.size(); ++i) same = same_action(ca[i], ma[i]);
                    if (!same)
                        std::printf("  %s (paused %d, shift %d, right %d): classic %zu cmds %zu acts, modern %zu cmds %zu acts\n",
                                    name, paused, shift, b == Button::Right, cc.size(), ca.size(), mc.size(), ma.size());
                    CHECK(same);
                    if (b == Button::Left) CHECK(cc.size() + ca.size() == 1);
                }
        }
        // the pause click is the pause command
        std::vector<commands::EventData> mc;
        h.build(f);
        h.modern.update_input(click_on(h.t.find("b_play_pause_t")), false, false, mc, nullptr);
        CHECK(mc.size() == 1 && mc[0] == pause_event(!paused));
    }
    // The shared controls are hit at the Classic component ids.
    Huds g;
    g.build(frame());
    for (const char* name : {"b_option_t", "b_play_pause_t", "b_fast_forward_t"}) {
        int32_t px = 0, py = 0;
        CHECK(centre_px(g.modern.hit_boxes(), g.modern.canvas(), g.t.find(name), px, py) &&
              g.modern.hit_test(px, py) == g.t.find(name));
    }
}

void test_alerts() {
    // The log: oldest first, aged by update, dropped after kSeconds, at most kMax, empty texts ignored.
    HudAlertLog log;
    log.push(HudAlert::Kind::Advisor, U"", 1.0);
    CHECK(log.items().empty());
    log.push(HudAlert::Kind::Advisor, U"Hold the line", 1.0);
    log.push_at(HudAlert::Kind::Radar, U"Under attack", 120.0f, -40.0f, 2.0);
    log.update(5.0);
    CHECK(log.items().size() == 2 && log.items()[0].age == 4.0f && log.items()[1].has_position && !log.items()[0].has_position);
    log.update(1.0 + HudAlertLog::kSeconds + 0.5);
    CHECK(log.items().size() == 1 && log.items()[0].text == U"Under attack");
    for (int i = 0; i < 20; ++i) log.push(HudAlert::Kind::Production, U"X", 3.0);
    CHECK(log.items().size() == HudAlertLog::kMax);
    // Radar events: the base-under-attack ping alerts, story blips and the player's clicks do not, others by type name.
    RadarEvent ping;
    ping.marker = true;
    RadarEvent blip = ping;
    blip.type = "Rebel_Base";
    RadarEvent click;
    click.type = "Default_Double_Click";
    RadarEvent beacon;
    beacon.type = "Default_Beacon_Placement";
    CHECK(radar_alert_text(ping, nullptr) == std::u32string(U"Base under attack") && !radar_alert_text(blip, nullptr) &&
          !radar_alert_text(click, nullptr) && radar_alert_text(beacon, nullptr) == std::u32string(U"Default Beacon Placement"));
    // The strip: the newest alert, the count of the others, Jump for a positioned one -> RadarCameraJump there.
    Huds h;
    HudAlertLog shown;
    shown.push(HudAlert::Kind::Advisor, U"Hold the line", 1.0);
    shown.push_at(HudAlert::Kind::Radar, U"Under attack", 120.0f, -40.0f, 2.0);
    TacticalHudFrame f = frame();
    f.alerts = shown.items();
    h.build(f);
    CHECK(h.modern.stats().alert_text == U"Under attack" && h.modern.stats().alerts_more == 1 && h.modern.stats().jump);
    std::vector<commands::EventData> cmds;
    std::vector<HudAction> acts;
    h.modern.update_input(click_on(kModernJumpHit), false, false, cmds, &acts);
    CHECK(cmds.empty() && acts.size() == 1 && acts[0].kind == HudAction::Kind::RadarCameraJump && acts[0].x == 120.0f &&
          acts[0].y == -40.0f);
    acts.clear();
    CHECK(h.modern.jump(acts) && acts.size() == 1 && acts[0].x == 120.0f);  // the AlertJump hotkey: the same jump
    // No position: no Jump, and the hotkey does nothing.
    HudAlertLog plain;
    plain.push(HudAlert::Kind::Objective, U"Destroy the station", 1.0);
    f.alerts = plain.items();
    h.build(f);
    acts.clear();
    CHECK(h.modern.stats().alert_text == U"Destroy the station" && !h.modern.stats().jump && !h.modern.jump(acts) && acts.empty());
    f.alerts = {};
    h.build(f);
    CHECK(h.modern.stats().alert_text.empty());
}

void test_objectives() {
    // The Classic entries, in order, with their texts (hud_objective_text); folded to the header while hidden.
    Huds h;
    std::vector<HudObjective> objs(3);
    objs[0].text_key = "TEXT_TACTICAL_PRIMARY_SPACE_ATTACK";
    objs[1].text = U"Protect the corvette";
    objs[1].state = HudObjective::State::Complete;
    objs[2].text = U"Do not lose the Home One";
    objs[2].state = HudObjective::State::Failed;
    TacticalHudFrame f = frame();
    f.objectives = objs;
    h.build(f);
    const ModernHudStats& s = h.modern.stats();
    CHECK(s.objectives_panel && !s.objectives_folded && s.objective_texts.size() == 3 && h.classic.stats().objectives == 3);
    for (size_t i = 0; i < objs.size() && i < s.objective_texts.size(); ++i) CHECK(s.objective_texts[i] == hud_objective_text(nullptr, objs[i]));
    CHECK(s.objective_texts[0] == U"TEXT_TACTICAL_PRIMARY_SPACE_ATTACK" && s.objective_texts[1] == U"Protect the corvette");
    f.objectives_hidden = true;
    h.build(f);
    CHECK(h.modern.stats().objectives_panel && h.modern.stats().objectives_folded && h.modern.stats().objective_texts.empty());
    f.objectives = {};
    h.build(f);
    CHECK(!h.modern.stats().objectives_panel);
}

void test_pause_banner() {
    // While paused, Modern shows the banner and its Resume Game sends the Classic banner's action (same component).
    Huds h;
    TacticalHudFrame f = frame();
    f.paused = true;
    h.build(f);
    CHECK(h.modern.stats().pause_banner && h.classic.stats().pause_banner);
    const int32_t resume = h.t.find("attack_button");
    int32_t px = 0, py = 0;
    CHECK(centre_px(h.modern.hit_boxes(), h.modern.canvas(), resume, px, py) && h.modern.hit_test(px, py) == resume);
    std::vector<commands::EventData> cc, mc;
    std::vector<HudAction> ca, ma;
    h.classic.update_input(click_on(resume), false, false, 1.0, cc, &ca);
    h.modern.update_input(click_on(resume), false, false, mc, &ma);
    CHECK(cc.empty() && mc.empty() && ca.size() == 1 && ma.size() == 1 && ca[0].kind == HudAction::Kind::ResumeGame &&
          same_action(ca[0], ma[0]));
    // No banner while running, or with the banner turned off (scripted captures).
    f.paused = false;
    h.build(f);
    CHECK(!h.modern.stats().pause_banner);
    f.paused = true;
    f.pause_banner = false;
    h.build(f);
    CHECK(!h.modern.stats().pause_banner);
}

void test_style_switch() {
    Huds h;
    h.build(frame());
    int32_t px = 0, py = 0;
    CHECK(centre_px(h.modern.hit_boxes(), h.modern.canvas(), kModernStyleSwitchHit, px, py) &&
          h.modern.hit_test(px, py) == kModernStyleSwitchHit);
    std::vector<commands::EventData> cmds;
    std::vector<HudAction> acts;
    h.modern.update_input(click_on(kModernStyleSwitchHit), false, false, cmds, &acts);
    CHECK(cmds.empty() && acts.size() == 1 && acts[0].kind == HudAction::Kind::HudStyleToggle);
    acts.clear();
    h.modern.update_input(click_on(kModernStyleSwitchHit, Button::Right), false, false, cmds, &acts);
    CHECK(cmds.empty() && acts.empty());
    // A panel's back takes the click and sends nothing; the deck covers the bottom middle of the screen.
    CHECK(h.modern.hit_test(960, 1000) == kModernPanelHit);
    h.modern.update_input(click_on(kModernPanelHit), false, false, cmds, &acts);
    CHECK(cmds.empty() && acts.empty());
    CHECK(h.modern.hit_test(960, 540) == -1);  // the battle in the middle is the world's
    // Interface hidden (the Toggle Interface key, movie mode): nothing drawn, nothing hit, a stale click sends nothing.
    TacticalHudFrame f = frame();
    f.world.interface_hidden = true;
    h.build(f);
    CHECK(h.modern.draw_list().total() == 0 && h.modern.hit_boxes().empty() && !h.modern.stats().status_corner);
    h.modern.update_input(click_on(kModernStyleSwitchHit), false, false, cmds, &acts);
    h.modern.update_input(click_on(h.t.find("b_option_t")), false, false, cmds, &acts);
    CHECK(cmds.empty() && acts.empty());
}

void test_text_scale() {
    Huds h;
    CHECK(h.modern.text_scale() == kModernTextScaleDefault);
    h.modern.set_text_scale(3.0f);
    CHECK(h.modern.text_scale() == kModernTextScaleMax);
    h.modern.set_text_scale(0.5f);
    CHECK(h.modern.text_scale() == kModernTextScaleMin);
    // All Modern text at the default scale is 18 px at 1080p (Xbox accessibility guideline 101).
    CHECK(font_pixel_height_int(kModernTextPx * kModernTextScaleDefault * 450.0f / 1080.0f, 1080) == 18);
    // A larger text scale makes a wider status corner.
    h.modern.set_text_scale(1.0f);
    h.modern.build(frame(), {}, {});
    float w1 = 0.0f, w2 = 0.0f;
    for (const HudHitBox& b : h.modern.hit_boxes())
        if (b.component == kModernPanelHit && b.max.y > 0.0f) w1 = b.max.x - b.min.x;
    h.modern.set_text_scale(2.0f);
    h.modern.build(frame(), {}, {});
    for (const HudHitBox& b : h.modern.hit_boxes())
        if (b.component == kModernPanelHit && b.max.y > 0.0f) w2 = b.max.x - b.min.x;
    CHECK(w1 > 0.0f && w2 > w1);
}

void test_hotkey() {
    // The port slot after the fleet group slots: HudStyleToggle on F9, an Interface event for the host.
    HotkeyTable t;
    CHECK(hk::kHudStyle == hk::kFleetLock + 1 && hk::kAlertJump == hk::kHudStyle + 1 && hk::kAlertJump + 1 == kHotkeySlots);
    CHECK(hotkey_slot("AlertJump") == hk::kAlertJump && t.binding(hk::kAlertJump).key == 0x73 &&
          t.binding(hk::kAlertJump).mod == HotkeyMod::None);
    CHECK(hotkey_slot("HudStyleToggle") == hk::kHudStyle && hotkey_slots()[hk::kHudStyle].kind == HotkeyKind::Interface);
    CHECK(t.binding(hk::kHudStyle).key == 0x78 && t.binding(hk::kHudStyle).mod == HotkeyMod::None);
    for (uint16_t s = 0; s < kHotkeySlots; ++s) CHECK(s == hk::kHudStyle || !(t.binding(s) == t.binding(hk::kHudStyle)));
    // A profile saved before the slot (166 slots) loads with the slot at its default...
    HotkeyProfile prof;
    t.save(prof);
    prof["KeySettingsCount"] = hk::kHudStyle;
    HotkeyTable old;
    CHECK(old.load(prof) && old.binding(hk::kHudStyle).key == 0x78);
    // ...unless the player put F9 on another port slot then: the new slot is unbound rather than shadowing it.
    HotkeyTable custom;
    custom.bind(hk::kFleetLock, {0x78, HotkeyMod::None});
    HotkeyProfile cprof;
    custom.save(cprof);
    cprof["KeySettingsCount"] = hk::kHudStyle;
    HotkeyTable cl;
    CHECK(cl.load(cprof) && cl.binding(hk::kFleetLock).key == 0x78 && cl.binding(hk::kHudStyle).key == -1);
    // The dispatcher hands the slot to the host as an event (no command).
    HotkeyDispatcher d;
    InputState in;
    InputFrame f;
    f.keys_pressed.set(0x78);
    f.keys_down.set(0x78);
    f.time = 1.0;
    in.update(f);
    std::vector<commands::EventData> cmds;
    std::vector<HotkeyEvent> evs;
    d.update(in, t, {}, 1.0, cmds, evs);
    CHECK(cmds.empty() && evs.size() == 1 && evs[0].slot == hk::kHudStyle);
}

void test_world_split() {
    // The Classic HUD with the split off is unchanged (world_draw_list empty); with it on, the drag box (a world-space
    // item) moves to world_draw_list and the two lists together hold what draw_list held.
    Huds h;
    TacticalHudFrame f = frame();
    f.drag_select = true;
    f.drag_rect = {100, 100, 400, 300};
    h.classic.build(f, {}, {});
    const size_t whole = h.classic.draw_list().total();
    const uint64_t digest = h.classic.draw_list().digest();
    CHECK(h.classic.world_draw_list().total() == 0 && h.classic.stats().drag_box);
    h.classic.set_world_split(true);
    h.classic.build(f, {}, {});
    CHECK(h.classic.world_draw_list().total() == 5);  // the drag box: fill + four edges
    CHECK(h.classic.draw_list().total() + h.classic.world_draw_list().total() == whole);
    h.classic.set_world_split(false);
    h.classic.build(f, {}, {});
    CHECK(h.classic.world_draw_list().total() == 0 && h.classic.draw_list().digest() == digest);
    // append: the Modern list after the world list
    HudDrawList a = h.classic.draw_list();
    h.modern.build(f, {}, {});
    a.append(h.modern.draw_list());
    CHECK(a.total() == whole + h.modern.draw_list().total());
}

}  // namespace

int main() {
    test_style_names();
    test_status_corner();
    test_deck();
    test_controls_parity();
    test_alerts();
    test_objectives();
    test_pause_banner();
    test_style_switch();
    test_text_scale();
    test_hotkey();
    test_world_split();
    if (g_fail) {
        std::printf("alamo_ui_hud_modern_tests: %d failure(s)\n", g_fail);
        return 1;
    }
    std::printf("alamo_ui_hud_modern_tests: all passed\n");
    return 0;
}
