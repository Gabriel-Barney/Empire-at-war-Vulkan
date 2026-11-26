// alamo_ui_hud_modern_tests (#584, docs/DECISIONS.md "Modern HUD: exception to the current focus"): the Modern tactical
// HUD style against the Classic one on the HUD seam (frame in; stats, hit boxes, commands and actions out): the status
// corner shows the Classic values, the shared game controls send the Classic commands and actions, the style switch
// asks the host to switch, the style names, the text scale, and the Classic world-space split.
#include <cstdio>
#include <memory>
#include <span>
#include <string_view>
#include <tuple>
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
  <CommandBarComponent Name="special_button_00"><Type>Button</Type></CommandBarComponent>
  <CommandBarComponent Name="special_button_01"><Type>Button</Type></CommandBarComponent>
  <CommandBarComponent Name="special_button_02"><Type>Button</Type></CommandBarComponent>
  <CommandBarComponent Name="special_button_03"><Type>Button</Type></CommandBarComponent>
  <CommandBarComponent Name="b_retreat"><Type>Button</Type></CommandBarComponent>
  <CommandBarComponent Name="b_reinforcement"><Type>Button</Type></CommandBarComponent>
  <CommandBarComponent Name="b_special_weapon"><Type>Button</Type></CommandBarComponent>
  <CommandBarComponent Name="b_special_weapon2"><Type>Button</Type></CommandBarComponent>
  <CommandBarComponent Name="Build_00"><Type>Button</Type></CommandBarComponent>
  <CommandBarComponent Name="Build_01"><Type>Button</Type></CommandBarComponent>
  <CommandBarComponent Name="Build_02"><Type>Button</Type></CommandBarComponent>
  <CommandBarComponent Name="tqueue00"><Type>Button</Type></CommandBarComponent>
  <CommandBarComponent Name="tqueue01"><Type>Button</Type></CommandBarComponent>
  <CommandBarComponent Name="r_0000"><Type>Button</Type></CommandBarComponent>
  <CommandBarComponent Name="r_0001"><Type>Button</Type></CommandBarComponent>
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
    // Radar events: an allow-list - only the base-under-attack ping alerts (story blips, clicks, beacons and the rest do
    // not; the host announces capture points itself).
    RadarEvent ping;
    ping.marker = true;
    RadarEvent blip = ping;
    blip.type = "Rebel_Base";
    RadarEvent click;
    click.type = "Default_Double_Click";
    RadarEvent beacon;
    beacon.type = "Default_Beacon_Placement";
    CHECK(radar_alert_text(ping, nullptr) == std::u32string(U"Base under attack") && !radar_alert_text(blip, nullptr) &&
          !radar_alert_text(click, nullptr) && !radar_alert_text(beacon, nullptr));
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
    {  // a hidden interface has no Jump: the hotkey does nothing after a hidden build
        TacticalHudFrame hidden = f;
        hidden.world.interface_hidden = true;
        h.build(hidden);
        acts.clear();
        CHECK(!h.modern.jump(acts) && acts.empty());
    }
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

// Every quad colour the Modern list draws.
bool draws_colour(const ModernTacticalHud& m, Rgba8 want) {
    for (uint32_t layer = 0; layer < kHudLayers; ++layer)
        for (uint32_t l = 0; l < kHudSubLists; ++l)
            for (const HudQuad& q : m.draw_list().items(layer, static_cast<HudList>(l)))
                if (q.color.r == want.r && q.color.g == want.g && q.color.b == want.b) return true;
    return false;
}

// #590 the Interface page's Modern options: key hints, order labels, the colour choices, the panel opacity and the text
// size; at the largest text every control stays on the screen.
void test_options() {
    Huds h;
    Selection s;
    TacticalHudFrame f = frame();
    f.world.local_player = 0;
    f.order_buttons.can_attack = true;
    HotkeyTable keys;
    h.modern.set_hotkeys(&keys);
    CHECK(h.modern.options() == ModernHudOptions{});
    h.modern.build(f, s.units, s.types);
    const uint32_t badges = h.modern.stats().key_badges;
    const size_t quads = h.modern.draw_list().total();
    CHECK(badges >= 5);
    ModernHudOptions o;
    o.key_hints = false;
    h.modern.set_options(o);
    h.modern.build(f, s.units, s.types);
    CHECK(h.modern.stats().key_badges == 0 && h.modern.draw_list().total() < quads);
    o = {};
    o.order_labels = false;  // the badge alone names the order: fewer glyphs, the same buttons and badges
    h.modern.set_options(o);
    h.modern.build(f, s.units, s.types);
    CHECK(h.modern.stats().orders == 6 && h.modern.stats().key_badges == badges && h.modern.draw_list().total() < quads);
    o.key_hints = false;  // both off: the order buttons keep their badges (else they would name nothing)
    h.modern.set_options(o);
    h.modern.build(f, s.units, s.types);
    CHECK(h.modern.stats().key_badges == 6);
    // Colours: colour-blind safe draws the Okabe-Ito blue accent, high contrast the yellow one; the faction look neither.
    o = {};
    h.modern.set_options(o);
    h.modern.build(f, s.units, s.types);
    CHECK(!draws_colour(h.modern, {0, 114, 178, 255}) && !draws_colour(h.modern, {255, 230, 0, 255}));
    o.colours = HudColours::ColourBlind;
    h.modern.set_options(o);
    h.modern.build(f, s.units, s.types);
    CHECK(draws_colour(h.modern, {0, 114, 178, 255}));
    o.colours = HudColours::HighContrast;
    h.modern.set_options(o);
    h.modern.build(f, s.units, s.types);
    CHECK(draws_colour(h.modern, {255, 230, 0, 255}));
    // Panel opacity: the panel back's alpha follows it.
    auto panel_alpha = [&] {
        for (const HudQuad& q : h.modern.draw_list().items(3, HudList::Alpha))  // frame_rect: the back on the Alpha list
            if (q.color.r == 10 && q.color.g == 14 && q.color.b == 22) return static_cast<int>(q.color.a);
        return -1;
    };
    o = {};
    o.panel_opacity = 40;
    h.modern.set_options(o);
    h.modern.build(f, s.units, s.types);
    const int a40 = panel_alpha();
    o.panel_opacity = 100;
    h.modern.set_options(o);
    h.modern.build(f, s.units, s.types);
    CHECK(a40 == 102 && panel_alpha() == 255);
    // The text size option is the text scale; out-of-range values are clamped.
    o = {};
    o.text_scale = 900;
    h.modern.set_options(o);
    CHECK(h.modern.options().text_scale == kModernTextScaleMaxPct && h.modern.text_scale() == kModernTextScaleMax);
    // The smallest text size keeps every label at the design minimum (#520: 14 px at 1080p); all Modern text uses font().
    CHECK(font_pixel_height_int(kModernTextPx * kModernTextScaleMin * 450.0f / 1080.0f, 1080) >= 14);
    // 200 % text, at 1080p and at 720p with a 200 % UI scale: every hit box and every glyph on the screen.
    std::vector<HudAlert> alerts(3);
    for (HudAlert& a : alerts) a.text = U"Base under attack at the northern landing pad";
    f.alerts = alerts;
    f.providers.abilities = [](const SelectedGroup&, std::vector<HudAbility>& out) {  // #586: with the abilities section
        for (uint8_t t = 1; t <= 4; ++t) {
            HudAbility a;
            a.ability_type = t;
            a.recharge = 0.25f;
            a.seconds_left = 30.0f;
            a.autofire = true;
            out.push_back(a);
        }
    };
    for (const auto& [w, hgt, ui] : {std::tuple{1920u, 1080u, 1.0f}, std::tuple{1280u, 720u, 2.0f}}) {
        h.modern.set_ui_scale(ui);  // takes effect on the resize
        h.modern.resize(w, hgt);
        h.modern.build(f, s.units, s.types);
        const HudCanvas& c = h.modern.canvas();
        const Vec2f lo = c.corners[3], hi = c.corners[1];
        CHECK(!h.modern.hit_boxes().empty());
        for (const HudHitBox& b : h.modern.hit_boxes()) {
            const bool inside = b.min.x >= lo.x - 0.5f && b.min.y >= lo.y - 0.5f && b.max.x <= hi.x + 0.5f && b.max.y <= hi.y + 0.5f;
            if (!inside)
                std::printf("  %ux%u ui %.1f: hit %d at (%.1f, %.1f)-(%.1f, %.1f) off the canvas (%.1f, %.1f)-(%.1f, %.1f)\n", w, hgt,
                            static_cast<double>(ui), b.component, static_cast<double>(b.min.x), static_cast<double>(b.min.y),
                            static_cast<double>(b.max.x), static_cast<double>(b.max.y), static_cast<double>(lo.x),
                            static_cast<double>(lo.y), static_cast<double>(hi.x), static_cast<double>(hi.y));
            CHECK(inside);
        }
        size_t glyphs = 0, off = 0;
        for (uint32_t layer = 0; layer < kHudLayers; ++layer)
            for (const HudQuad& q : h.modern.draw_list().items(layer, HudList::Text)) {
                ++glyphs;
                if (q.min.x < lo.x - 0.5f || q.min.y < lo.y - 0.5f || q.max.x > hi.x + 0.5f || q.max.y > hi.y + 0.5f) ++off;
            }
        CHECK(glyphs > 0 && off == 0);
    }
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

// #586 the abilities section: the Classic buttons (merge, users, order), the same clicks and hotkey, and the ring / AUTO /
// active states from the feed.
void test_abilities() {
    Huds h;
    Selection s;
    TacticalHudFrame f = frame();
    f.world.local_player = 0;
    float recharge9 = 0.4f;
    bool auto7 = true;
    f.providers.abilities = [&](const SelectedGroup& g, std::vector<HudAbility>& out) {
        HudAbility a;
        if (g.type == 0) {  // X-wings: 5 ready, 9 targeted and recharging
            a.ability_type = 5;
            out.push_back(a);
            a = {};
            a.ability_type = 9;
            a.targeted = true;
            a.target_kind = 2;
            a.recharge = recharge9;
            a.seconds_left = recharge9 < 1.0f ? 12.2f : -1.0f;
            out.push_back(a);
        } else {  // the corvette: 5 on, 7 disabled with autofire on
            a.ability_type = 5;
            a.active = true;
            a.recharge = 0.5f;
            out.push_back(a);
            a = {};
            a.ability_type = 7;
            a.enabled = false;
            a.autofire = auto7;
            out.push_back(a);
        }
    };
    h.build(f, s.units, s.types);
    const ModernHudStats& st = h.modern.stats();
    CHECK(st.abilities == 3 && st.ability_rings == 1 && st.ability_auto == 1 && st.ability_active == 1);
    CHECK(st.ability_counts.size() == 1 && st.ability_counts[0] == U"13");  // 12.2 s left -> 13
    CHECK(h.classic.stats().ability_buttons == 3);
    // Clicks: left, Shift-left (queued) and right on each button = Classic's, the disabled one nothing in either.
    for (int32_t i = 0; i < 3; ++i) {
        char n[24];
        std::snprintf(n, sizeof n, "special_button_%02d", i);
        const int32_t id = h.t.find(n);
        for (const auto& [b, shift] : {std::pair{Button::Left, false}, std::pair{Button::Left, true}, std::pair{Button::Right, false}}) {
            h.build(f, s.units, s.types);
            size_t emitted = 0;
            CHECK(h.same_click(id, id, shift, false, b, &emitted));
            CHECK(emitted == (i == 2 ? 0u : 1u));
        }
    }
    // The ability hotkey: activate_ability agrees for every type (one shown, one targeted, one disabled, one absent).
    h.build(f, s.units, s.types);
    for (const uint8_t type : {uint8_t{5}, uint8_t{9}, uint8_t{7}, uint8_t{99}}) {
        std::vector<commands::EventData> cc, mc;
        std::vector<HudAction> ca, ma;
        const bool cr = h.classic.activate_ability(type, true, cc, &ca), mr = h.modern.activate_ability(type, true, mc, &ma);
        bool same = cr == mr && cc == mc && ca.size() == ma.size();
        for (size_t i = 0; same && i < ca.size(); ++i) same = same_action(ca[i], ma[i]) && ca[i].ability_type == ma[i].ability_type;
        CHECK(same);
        CHECK(mr == (type == 5 || type == 9));
    }
    // The ring and AUTO follow the feed.
    recharge9 = 1.0f;
    auto7 = false;
    h.build(f, s.units, s.types);
    CHECK(st.abilities == 3 && st.ability_rings == 0 && st.ability_auto == 0);
    // The story's ability arrow finds the drawn Modern button; an absent type none.
    Vec2f bc, bs;
    CHECK(h.modern.ability_box(9, bc, bs) && bs.x > 0.0f && !h.modern.ability_box(99, bc, bs));
    // Key badges: the binding of the slot that fires the type (ability_hotkey_slot), with the live table.
    uint16_t slot = 0;
    while (slot < kHotkeySlots && hotkey_ability_name(slot).empty()) ++slot;
    const std::string bound(hotkey_ability_name(slot));
    HotkeyTable keys;
    CHECK(!bound.empty() && ability_hotkey_slot(keys, bound) >= 0 && ability_hotkey_slot(keys, "NO_SUCH_ABILITY") < 0);
    h.modern.set_hotkeys(&keys);
    f.providers.abilities = [&](const SelectedGroup& g, std::vector<HudAbility>& out) {
        if (g.type != 0) return;
        HudAbility a;
        a.ability_type = 5;
        a.type_name = bound;
        out.push_back(a);
    };
    h.build(f, s.units, s.types);
    const uint32_t with_badge = st.key_badges;
    f.providers.abilities = [&](const SelectedGroup& g, std::vector<HudAbility>& out) {
        if (g.type != 0) return;
        HudAbility a;
        a.ability_type = 5;
        out.push_back(a);
    };
    h.build(f, s.units, s.types);
    CHECK(st.abilities == 1 && with_badge == st.key_badges + 1);
    h.modern.set_hotkeys(nullptr);
    // More abilities than the deck has room for (and than the table has special buttons): the hotkey agrees with Classic
    // for every one, drawn or not.
    f.providers.abilities = [](const SelectedGroup&, std::vector<HudAbility>& out) {
        for (uint8_t t = 1; t <= 12; ++t) {
            HudAbility a;
            a.ability_type = t;
            out.push_back(a);
        }
    };
    h.build(f, s.units, s.types);
    CHECK(st.abilities <= 4);
    for (uint8_t type = 1; type <= 12; ++type) {
        std::vector<commands::EventData> cc, mc;
        std::vector<HudAction> ca, ma;
        const bool cr = h.classic.activate_ability(type, false, cc, &ca), mr = h.modern.activate_ability(type, false, mc, &ma);
        CHECK(cr == mr && cr && cc == mc && ca.size() == ma.size());
    }
    // No provider or no own selection: no section.
    f.providers.abilities = nullptr;
    h.build(f, s.units, s.types);
    CHECK(st.abilities == 0);
    // The merge keeps the seconds left of the readiest group, the shorter on a tie.
    HudProviders p;
    p.abilities = [](const SelectedGroup& g, std::vector<HudAbility>& out) {
        HudAbility a;
        a.ability_type = 3;
        a.recharge = 0.5f;
        a.seconds_left = g.type == 0 ? 20.0f : 8.0f;
        out.push_back(a);
    };
    std::vector<SelectedGroup> two(2);
    two[0].type = 0;
    two[0].ids = {1};
    two[1].type = 1;
    two[1].ids = {2};
    const std::vector<HudAbilityButton> merged = selection_ability_buttons(two, p, 48);
    CHECK(merged.size() == 1 && merged[0].ability.seconds_left == 8.0f && (merged[0].users == std::vector<uint32_t>{1, 2}));
}

// #587 the battle actions (always), the shortcuts and the contextual deck, production and the reinforcement list: the
// sections follow the selection, and every click is Classic's.
void test_actions() {
    Huds h;
    Selection s;
    TacticalHudFrame f = frame();
    f.world.local_player = 0;
    f.command_buttons.present = true;
    f.command_buttons.retreat_enabled = true;
    f.command_buttons.retreat_dial = 0.4f;
    f.command_buttons.reinforce_enabled = true;
    f.command_buttons.reinforce_count = 3;
    f.command_buttons.beacon_enabled = false;
    f.bombing_run.present = true;
    f.bombing_run.disabled = false;
    f.bombing_run.dial = 0.5f;
    std::vector<HudUnit> none = s.units;
    for (HudUnit& u : none) u.selected = false;
    h.build(f, none, s.types);
    const ModernHudStats& st = h.modern.stats();
    // Nothing selected: the actions, no cards / orders, and (contextual on) a deck shrunk to them.
    CHECK(st.actions == 4 && st.actions_live == 3 && st.cards == 0 && st.orders == 0 && !st.production);
    const float full = h.modern.canvas().width - 16.0f;
    CHECK(st.deck_width > 0.0f && st.deck_width < 0.5f * full);
    for (const auto& [name, n] : {std::pair{"b_retreat", 1u}, std::pair{"b_reinforcement", 1u}, std::pair{"b_special_weapon", 1u},
                                  std::pair{"b_special_weapon2", 0u}}) {
        h.build(f, none, s.types);
        size_t emitted = 0;
        CHECK(h.same_click(h.t.find(name), h.t.find(name), false, false, Button::Left, &emitted));
        CHECK(emitted == n);
    }
    {
        std::vector<commands::EventData> cmds;
        h.modern.update_input(click_on(h.t.find("b_retreat")), false, false, cmds, nullptr);
        CHECK(cmds.size() == 1 && cmds[0] == commands::ev::retreat(true));
    }
    // A Lua GUI_Component_Visibility / _Enable override on Classic's buttons applies in Modern too: retreat hidden, beacon on.
    {
        HudButtons script;
        script.bind(h.t);
        script.state(static_cast<uint32_t>(h.t.find("b_retreat"))).script_visible = 0;
        script.state(static_cast<uint32_t>(h.t.find("b_special_weapon2"))).script_enabled = 1;
        h.modern.set_script_state(&script);
        h.build(f, none, s.types);
        CHECK(st.actions == 3 && st.actions_live == 3);
        h.modern.set_script_state(nullptr);
    }
    // The contextual option off: the deck is always full width.
    ModernHudOptions o;
    o.contextual = false;
    h.modern.set_options(o);
    h.build(f, none, s.types);
    CHECK(std::fabs(st.deck_width - full) < 0.5f);
    h.modern.set_options({});
    // Shortcuts: a wing, a fleet group (its members) and the station.
    HudWing wing;
    wing.label = U"FIGHTERS";
    wing.squadrons = 1;
    wing.ids = {40, 41};
    std::vector<HudWing> wings{wing};
    f.wings = wings;
    HudFleetGroup g;
    g.id = 2;
    g.members = {20, 21};
    std::vector<HudFleetGroup> groups{g};
    f.fleet_groups_on = true;
    f.fleet_groups = groups;
    f.station = 77;
    h.build(f, none, s.types);
    CHECK(st.shortcuts == 3);
    std::vector<commands::EventData> cmds;
    h.modern.update_input(click_on(kModernStationHit), false, false, cmds, nullptr);
    h.build(f, none, s.types);
    h.modern.update_input(click_on(kModernFleetShortcutBase), false, false, cmds, nullptr);
    CHECK(cmds.size() == 2 && cmds[0] == commands::ev::select({77}) && cmds[1] == commands::ev::select({20, 21}));
    h.build(f, none, s.types);
    size_t wn = 0;
    CHECK(h.same_click(kWingHitBase, kWingHitBase, false, false, Button::Left, &wn) && wn == 1);
    f.wings = {};
    f.fleet_groups_on = false;
    f.fleet_groups = {};
    f.station = commands::kNoObject;
    // Production: the selected builder's card, options (cost / blocked reason) and queue; the clicks are Classic's.
    std::vector<HudBuildOption> opts(3);
    opts[0].type_index = 1;
    opts[0].cost = 1200;
    opts[1].type_index = 0;
    opts[1].block = HudBuildOption::Block::Funds;
    opts[2].type_index = 1;
    opts[2].block = HudBuildOption::Block::QueueFull;
    opts[2].queued = 2;
    f.builder = 22;
    f.build_options = opts;
    f.providers.build_queue = [](std::vector<HudQueueEntry>& out) {
        HudQueueEntry e;
        e.type_index = 1;
        e.progress = 0.3f;
        e.location = 22;
        e.index = 0;
        out.push_back(e);
        e.index = 1;
        e.progress = 0.0f;
        out.push_back(e);
    };
    h.build(f, s.units, s.types);
    CHECK(st.production && st.build_options == 3 && st.build_blocked == 2 && st.queue_entries == 2 && st.cards == 0 && st.orders == 6);
    CHECK(st.build_texts.size() == 3 && st.build_texts[0] == U"$ 1,200" && st.build_texts[1] == U"Not enough credits");
    for (const auto& [name, n, shift] : {std::tuple{"Build_00", 1u, false}, std::tuple{"Build_00", 1u, true}, std::tuple{"Build_01", 0u, false},
                                         std::tuple{"tqueue00", 1u, false}, std::tuple{"tqueue01", 1u, false}}) {
        h.build(f, s.units, s.types);
        size_t emitted = 0;
        CHECK(h.same_click(h.t.find(name), h.t.find(name), shift, false, Button::Left, &emitted));
        CHECK(emitted == n);
    }
    // The queue: with production, only the selected builder's entries; with nothing selected, the whole queue (Classic
    // shows it whatever is selected), its cancel Classic's.
    auto two_builders = [](std::vector<HudQueueEntry>& out) {
        HudQueueEntry e;
        e.type_index = 1;
        e.location = 22;
        out.push_back(e);
        e.location = 99;  // another builder's
        out.push_back(e);
    };
    f.providers.build_queue = two_builders;
    h.build(f, s.units, s.types);
    CHECK(st.production && st.queue_entries == 1);
    f.builder = commands::kNoObject;
    f.build_options = {};
    h.build(f, none, s.types);
    CHECK(!st.production && st.queue_entries == 2);
    for (const char* name : {"tqueue00", "tqueue01"}) {
        h.build(f, none, s.types);
        size_t emitted = 0;
        CHECK(h.same_click(h.t.find(name), h.t.find(name), false, false, Button::Left, &emitted) && emitted == 1);
    }
    f.providers.build_queue = nullptr;
    // The reinforcement list while open: its entries; a call starts the drop point pick, as Classic's.
    f.reinforce_list_open = true;
    h.classic.toggle_reinforce_list(true);
    f.providers.reinforcements = [](std::vector<HudReinforcement>& out) {
        HudReinforcement r;
        r.unit = 90;
        r.count = 2;
        out.push_back(r);
        r.unit = 91;
        r.available = false;
        out.push_back(r);
    };
    h.build(f, none, s.types);
    CHECK(st.reinforcements == 2 && st.actions == 4);
    for (const auto& [name, n] : {std::pair{"r_0000", 1u}, std::pair{"r_0001", 0u}}) {
        h.build(f, none, s.types);
        size_t emitted = 0;
        CHECK(h.same_click(h.t.find(name), h.t.find(name), false, false, Button::Left, &emitted));
        CHECK(emitted == n);
    }
}

// #588 fleet group tabs: the Classic grouping (group_selection_fleets) under each group's tab, Merge on the oldest tab
// when the selection spans groups, Split on a partly selected group; every click Classic's.
void test_fleet_tabs() {
    Huds h;
    Selection s;
    TacticalHudFrame f = frame();
    f.world.local_player = 0;
    std::vector<HudFleetGroup> fleets(2);
    fleets[0].id = 1;
    fleets[0].members = {20, 21};
    fleets[1].id = 2;
    fleets[1].name = U"Escort";
    fleets[1].locked = true;
    fleets[1].members = {22, 50};  // 50 not selected: a partial selection
    f.fleet_groups_on = true;
    f.fleet_groups = fleets;
    h.build(f, s.units, s.types);
    const ModernHudStats& st = h.modern.stats();
    CHECK(st.fleet_tabs == 2 && st.fleet_merge == 1 && st.fleet_split == 1);
    CHECK(h.classic.stats().fleet_tabs == 2);
    CHECK(h.modern.selection_cards().size() == h.classic.panel_groups().size());
    for (size_t i = 0; i < h.modern.selection_cards().size() && i < h.classic.panel_groups().size(); ++i)
        CHECK(h.modern.selection_cards()[i].ids == h.classic.panel_groups()[i].ids &&
              h.modern.selection_cards()[i].fleet == h.classic.panel_groups()[i].fleet);
    // The shared rules: Merge on the oldest selected group's tab, Split on the partly selected one.
    const std::vector<size_t> sel = fleet_selected_counts(s.units, fleets);
    CHECK((sel == std::vector<size_t>{2, 1}));
    const HudFleetTabState t0 = fleet_tab_state(fleets, sel, 1), t1 = fleet_tab_state(fleets, sel, 2);
    CHECK(t0.merge && !t0.split && !t1.merge && t1.split && t1.group == &fleets[1]);
    // Clicks: the tab (left, Shift-left, right = rename), Merge, Split = Classic's.
    for (const auto& [id, b, shift] : {std::tuple{kFleetTabHitBase, Button::Left, false}, std::tuple{kFleetTabHitBase + 1, Button::Left, true},
                                       std::tuple{kFleetTabHitBase + 1, Button::Right, false}, std::tuple{kFleetMergeHitBase, Button::Left, false},
                                       std::tuple{kFleetSplitHitBase + 1, Button::Left, false}}) {
        h.build(f, s.units, s.types);
        size_t emitted = 0;
        std::vector<commands::EventData> cc, mc;
        std::vector<HudAction> ca, ma;
        h.classic.update_input(click_on(id, b), shift, false, 1.0, cc, &ca);
        h.modern.update_input(click_on(id, b), shift, false, mc, &ma);
        CHECK(cc == mc && ca.size() == ma.size());
        for (size_t i = 0; i < ca.size() && i < ma.size(); ++i) CHECK(same_action(ca[i], ma[i]));
        emitted = mc.size() + ma.size();
        CHECK(emitted == 1);
    }
    // A one-card run with a name and both buttons, followed by another run: its Merge and Split stay the topmost hit at
    // their centres (a layer over the tabs, the tab clamped to its room).
    {
        std::vector<HudFleetGroup> fl(2);
        fl[0].id = 1;
        fl[0].name = U"A very long group name for the first run";
        fl[0].members = {20, 50};  // partly selected: Split; the oldest of two: Merge
        fl[1].id = 2;
        fl[1].members = {21, 22};
        std::vector<HudUnit> us = s.units;
        TacticalHudFrame g = f;
        g.fleet_groups = fl;
        h.modern.build(g, us, s.types);
        CHECK(st.fleet_merge == 1 && st.fleet_split == 1);
        for (const int32_t id : {kFleetMergeHitBase, kFleetSplitHitBase}) {
            int32_t px = 0, py = 0;
            CHECK(centre_px(h.modern.hit_boxes(), h.modern.canvas(), id, px, py) && h.modern.hit_test(px, py) == id);
        }
    }
    // Fleet groups off: no tabs.
    f.fleet_groups_on = false;
    h.build(f, s.units, s.types);
    CHECK(st.fleet_tabs == 0);
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
    test_options();
    test_abilities();
    test_actions();
    test_fleet_tabs();
    test_hotkey();
    test_world_split();
    if (g_fail) {
        std::printf("alamo_ui_hud_modern_tests: %d failure(s)\n", g_fail);
        return 1;
    }
    std::printf("alamo_ui_hud_modern_tests: all passed\n");
    return 0;
}
