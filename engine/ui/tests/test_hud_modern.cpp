// alamo_ui_hud_modern_tests (#584, docs/DECISIONS.md "Modern HUD: exception to the current focus"): the Modern tactical
// HUD style against the Classic one on the HUD seam (frame in; stats, hit boxes, commands and actions out): the status
// corner shows the Classic values, the shared game controls send the Classic commands and actions, the style switch
// asks the host to switch, the style names, the text scale, and the Classic world-space split. #593: the Modern galactic
// view against the Classic GalacticHud (the same clicks, the Classic overlay, the selection kept across a switch).
#include <cstdio>
#include <memory>
#include <span>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

#include "alamo/data/xml.hpp"
#include "alamo/test/support.hpp"
#include "alamo/ui/camera.hpp"
#include "alamo/ui/galactic_hud.hpp"
#include "alamo/ui/hotkeys.hpp"
#include "alamo/ui/hud_font.hpp"
#include "alamo/ui/modern_galactic_hud.hpp"
#include "alamo/ui/modern_tactical_hud.hpp"
#include "alamo/ui/orders.hpp"
#include "alamo/ui/picking.hpp"
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

bool has_hit(const std::vector<HudHitBox>& hits, int32_t id) {
    return std::any_of(hits.begin(), hits.end(), [id](const HudHitBox& h) { return h.component == id; });
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
    // The full row: the screen less the margins and the map at its end (#591).
    const HudCanvas& hc = h.modern.canvas();
    const float full = hc.width - 16.0f - (std::min(0.3f * hc.height, 0.22f * hc.width) + 8.0f);
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
    // Shortcuts: the station (#601: the wing and the fleet group are the roster's cards, with their clicks).
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
    CHECK(st.shortcuts == 1 && st.roster && st.wings == 1 && st.roster_groups == 1);
    std::vector<commands::EventData> cmds;
    h.modern.update_input(click_on(kModernStationHit), false, false, cmds, nullptr);
    h.build(f, none, s.types);
    h.modern.update_input(click_on(kModernRosterGroupBase), false, false, cmds, nullptr);
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
    CHECK(st.build_texts.size() == 3 && st.build_texts[0] == U"1200" && st.build_texts[1] == U"Not enough credits");
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

// #591 the map: blips by shape (own squares, hostile diamonds, neutral circles) with Classic's filter; the map side; the
// radar clicks (press / drag jump, right click move + ping). The hover target card: hull, shields and hard points.
void test_map() {
    Huds h;
    Selection s;
    TacticalHudFrame f = frame();
    f.world.local_player = 0;
    CameraParams cp;
    cp.eye = {0.0f, -600.0f, 600.0f};
    cp.target = {0.0f, 0.0f, 0.0f};
    f.world.view = make_view(cp, 1920, 1080);
    std::vector<HudUnit> units = s.units;
    HudUnit neutral;
    neutral.id = 60;
    neutral.owner = -1;
    units.push_back(neutral);
    HudUnit jammed = units[3];  // a jammed hostile: no blip (radar_blip_shown, as Classic's)
    jammed.id = 61;
    jammed.jammed = true;
    units.push_back(jammed);
    h.modern.build(f, units, s.types);
    const ModernHudStats& st = h.modern.stats();
    CHECK(st.map && !st.map_right && st.map_squares == 3 && st.map_diamonds == 1 && st.map_circles == 1);
    CHECK(radar_blip_shown(units[3], nullptr, true) && !radar_blip_shown(jammed, nullptr, true));
    // The map sits at the deck's end, level with it: no deck control overlaps it.
    for (const HudHitBox& m : h.modern.hit_boxes())
        if (m.component == kModernMapHit)
            for (const HudHitBox& b : h.modern.hit_boxes())
                if (b.component != kModernMapHit && b.component != kModernPanelHit)
                    CHECK(b.max.x <= m.min.x || b.min.x >= m.max.x || b.max.y <= m.min.y || b.min.y >= m.max.y);
    // The map's centre is the world's (0, 0): a press there jumps the camera there; a right click moves the selection.
    int32_t px = 0, py = 0;
    CHECK(centre_px(h.modern.hit_boxes(), h.modern.canvas(), kModernMapHit, px, py) && h.modern.hit_test(px, py) == kModernMapHit);
    {
        RoutedInput r;
        r.hover_component = kModernMapHit;
        r.owner[static_cast<size_t>(Button::Left)] = PointerTarget::Hud;
        r.world.mouse_x = px;
        r.world.mouse_y = py;
        r.hud = {{HudPointerEvent::Kind::Press, Button::Left, kModernMapHit, px, py}};
        std::vector<commands::EventData> cmds;
        std::vector<HudAction> acts;
        h.modern.update_input(r, false, false, cmds, &acts);
        CHECK(cmds.empty() && acts.size() == 1 && acts[0].kind == HudAction::Kind::RadarCameraJump && std::fabs(acts[0].x) < 60.0f &&
              std::fabs(acts[0].y) < 60.0f);
        r.hud = {{HudPointerEvent::Kind::Release, Button::Left, kModernMapHit, px, py},
                 {HudPointerEvent::Kind::Press, Button::Right, kModernMapHit, px, py},
                 {HudPointerEvent::Kind::Release, Button::Right, kModernMapHit, px, py},
                 {HudPointerEvent::Kind::Click, Button::Right, kModernMapHit, px, py}};
        cmds.clear();
        acts.clear();
        h.modern.update_input(r, false, false, cmds, &acts);
        CHECK(cmds.size() == 1 && acts.size() == 1 && acts[0].kind == HudAction::Kind::RadarPing && acts[0].name == "Default_Click");
    }
    // The map side option: the map on the right.
    ModernHudOptions o;
    o.map_right = true;
    h.modern.set_options(o);
    h.modern.build(f, units, s.types);
    CHECK(st.map_right);
    for (const HudHitBox& b : h.modern.hit_boxes())
        if (b.component == kModernMapHit) CHECK(b.min.x > 0.0f);
    h.modern.set_options({});
    // The hover target card: the hovered hostile's hull, shields and hard points; it takes no clicks.
    units[3].hovered = true;
    units[3].health = 0.4f;
    units[3].shield = 0.75f;
    std::vector<HudHardPoint> hps(3);
    for (HudHardPoint& hp : hps) hp.parent = 30;
    hps[2].parent = 99;  // another ship's
    f.hardpoints = hps;
    const size_t hits_before = [&] {
        h.modern.build(f, s.units, s.types);
        return h.modern.hit_boxes().size();
    }();
    h.modern.build(f, units, s.types);
    CHECK(st.target_card && st.target_id == 30 && st.target_hardpoints == 2);
    CHECK(h.modern.hit_boxes().size() == hits_before);
    units[3].hovered = false;
    units[0].hovered = true;  // an own unit: no card
    h.modern.build(f, units, s.types);
    CHECK(!st.target_card);
}

// #592 tactical land: the reinforcement zones on the map (enabling solid, prevention dashed); the build pad's buy
// buttons on the pad (Classic's placement) with the same options, costs, blocked reasons and clicks; the bombing run and
// a reinforcement call are #587's (test_actions).
void test_land() {
    Huds h;
    Selection s;
    TacticalHudFrame f = frame();
    f.world.local_player = 0;
    f.world.land = true;
    std::vector<HudDropZone> zones{{0.0f, 0.0f, 800.0f, true}, {1500.0f, 1500.0f, 600.0f, false}, {0.0f, 0.0f, 0.0f, true}};
    f.drop_zones = zones;
    h.modern.build(f, s.units, s.types);
    const ModernHudStats& st = h.modern.stats();
    CHECK(st.map_drop_zones == 1 && st.map_block_zones == 1);  // a zero radius is not drawn
    // The pad: a land builder with its anchor on screen.
    std::vector<HudBuildOption> opts(3);
    opts[0].type_index = 1;
    opts[0].cost = 300;
    opts[1].type_index = 0;
    opts[1].block = HudBuildOption::Block::PopCap;
    opts[2].type_index = 1;
    opts[2].cost = 450;
    opts[2].queued = 3;
    f.builder = 22;
    f.build_options = opts;
    f.has_build_anchor = true;
    f.build_anchor_px = {960.0f, 400.0f};
    h.build(f, s.units, s.types);
    CHECK(st.production && st.pad_options && st.build_options == 3 && st.build_blocked == 1 && h.classic.stats().build_options == 3);
    CHECK(st.build_texts.size() == 3 && st.build_texts[0] == U"300" && st.build_texts[1] == U"Population cap" && st.build_texts[2] == U"450");
    CHECK(build_option_text(opts[0], nullptr) == U"300" && build_option_text(opts[1], nullptr) == U"Population cap");
    // The buttons sit round the pad's point (above the deck), each where Classic's hit box is.
    for (const char* name : {"Build_00", "Build_02"}) {
        int32_t mx = 0, my = 0, cx = 0, cy = 0;
        CHECK(centre_px(h.modern.hit_boxes(), h.modern.canvas(), h.t.find(name), mx, my) &&
              centre_px(h.classic.hit_boxes(), h.classic.canvas(), h.t.find(name), cx, cy));
        CHECK(std::abs(mx - cx) <= 2 && std::abs(my - cy) <= 2);
    }
    for (const auto& [name, n] : {std::pair{"Build_00", 1u}, std::pair{"Build_01", 0u}, std::pair{"Build_02", 1u}}) {
        h.build(f, s.units, s.types);
        size_t emitted = 0;
        CHECK(h.same_click(h.t.find(name), h.t.find(name), false, false, Button::Left, &emitted));
        CHECK(emitted == n);
    }
    // The open reinforcement list takes the deck; the pad keeps its buttons.
    f.reinforce_list_open = true;
    h.classic.toggle_reinforce_list(true);
    f.providers.reinforcements = [](std::vector<HudReinforcement>& out) {
        HudReinforcement r;
        r.unit = 90;
        out.push_back(r);
    };
    h.build(f, s.units, s.types);
    CHECK(st.reinforcements == 1 && st.pad_options && st.build_options == 3);
    // On land: a reinforcement call (to the drop point the host then picks) and the bombing run are Classic's.
    f.bombing_run.present = true;
    f.bombing_run.disabled = false;
    f.command_buttons.present = true;
    f.command_buttons.reinforce_enabled = true;
    for (const auto& [name, n] : {std::pair{"r_0000", 1u}, std::pair{"b_special_weapon", 1u}, std::pair{"b_reinforcement", 1u}}) {
        h.build(f, s.units, s.types);
        size_t emitted = 0;
        CHECK(h.same_click(h.t.find(name), h.t.find(name), false, false, Button::Left, &emitted));
        CHECK(emitted == n);
    }
    f.reinforce_list_open = false;
    f.providers.reinforcements = nullptr;
    // Space (or no anchor): the options are in the deck.
    f.world.land = false;
    h.build(f, s.units, s.types);
    CHECK(st.production && !st.pad_options && st.build_options == 3);
}

// #600 the tabs' state chips and health: a free and a locked group (chips by the lock flag, a health bar each, the
// feed's health first), Merge / Split at least 24 px tall at 1080p; a wing run's tab (fleet groups off too) clicks as
// Classic's wing button; with fleet groups off no group tab.
void test_tab_chips() {
    Huds h;
    Selection s;
    TacticalHudFrame f = frame();
    f.world.local_player = 0;
    std::vector<HudFleetGroup> fleets(2);
    fleets[0].id = 1;
    fleets[0].members = {20, 21};
    fleets[0].health = 0.4f;
    fleets[1].id = 2;
    fleets[1].locked = true;
    fleets[1].members = {22, 50};
    f.fleet_groups_on = true;
    f.fleet_groups = fleets;
    h.modern.build(f, s.units, s.types);
    const ModernHudStats& st = h.modern.stats();
    CHECK(st.fleet_tabs == 2 && st.chips_free == 1 && st.chips_locked == 1 && st.chips_wing == 0 && st.tab_health_bars == 2 && st.wing_tabs == 0);
    CHECK(st.fleet_merge == 1 && st.fleet_split == 1);  // the #177 rules (test_fleet_tabs: their clicks)
    size_t sized = 0;
    for (const HudHitBox& b : h.modern.hit_boxes())
        if ((b.component >= kFleetMergeHitBase && b.component < kFleetMergeHitBase + 8) ||
            (b.component >= kFleetSplitHitBase && b.component < kFleetSplitHitBase + 8)) {
            const Vec2f lo = h.modern.canvas().canvas_to_pixel(b.min.x, b.min.y), hi = h.modern.canvas().canvas_to_pixel(b.max.x, b.max.y);
            CHECK(std::fabs(hi.y - lo.y) >= 24.0f - 0.5f);
            ++sized;
        }
    CHECK(sized == 2);
    fleets[0].locked = true;
    h.modern.build(f, s.units, s.types);
    CHECK(st.chips_locked == 2 && st.chips_free == 0);
    // A wing: the X-wings (20, 21) are the fighter wing.
    std::vector<HudWing> wings(1);
    wings[0].label = U"FIGHTERS";
    wings[0].hotkey = U"F2";
    wings[0].squadrons = 1;
    wings[0].ids = {20, 21};
    wings[0].craft_alive = 2;
    wings[0].craft_total = 3;
    wings[0].health = 0.6f;
    f.wings = wings;
    f.fleet_groups_on = false;
    f.fleet_groups = {};
    h.build(f, s.units, s.types);
    CHECK(st.wing_tabs == 1 && st.fleet_tabs == 0 && st.chips_wing == 1 && st.tab_health_bars == 1 &&
          has_hit(h.modern.hit_boxes(), kModernWingTabBase));
    size_t emitted = 0;
    CHECK(h.same_click(kWingHitBase, kModernWingTabBase, false, false, Button::Left, &emitted));
    CHECK(emitted == 1);
    h.build(f, s.units, s.types);
    CHECK(h.same_click(kWingHitBase, kModernWingTabBase, true, false));
    // Fleet groups on: the grouped X-wings are their group's run, not the wing's.
    fleets[0].locked = false;
    f.fleet_groups_on = true;
    f.fleet_groups = fleets;
    h.modern.build(f, s.units, s.types);
    CHECK(st.fleet_tabs == 2 && st.wing_tabs == 0);
}

// ---- #599 the fleet discipline control ----

// The commands the Classic key of `slot` sends (HotkeyDispatcher, fleet groups `on`, `locked` = the selection's groups
// all locked) for `selection`.
std::vector<commands::EventData> key_commands(uint16_t slot, bool on, bool locked, std::span<const uint32_t> selection) {
    HotkeyTable t;
    HotkeyDispatcher d;
    d.set_fleet_groups(on, locked);
    InputState in;
    InputFrame fr;
    fr.keys_pressed.set(static_cast<size_t>(t.binding(slot).key));
    fr.keys_down.set(static_cast<size_t>(t.binding(slot).key));
    fr.time = 1.0;
    in.update(fr);
    std::vector<commands::EventData> cmds;
    std::vector<HotkeyEvent> evs;
    d.update(in, t, selection, 1.0, cmds, evs);
    return cmds;
}

// The pure rule over every combination: fleet groups on / off, cohesion Loose / Tight, each of two groups locked or not,
// and selections with no group, one group, both groups, a group and an ungrouped unit.
void test_discipline_rule() {
    std::vector<HudFleetGroup> groups(2);
    groups[0].id = 1;
    groups[0].members = {10, 11};
    groups[1].id = 2;
    groups[1].members = {20};
    const std::vector<std::vector<uint32_t>> selections = {{}, {30}, {10}, {10, 20}, {11, 30}};
    uint32_t cases = 0;
    for (const bool on : {false, true})
        for (const bool tight : {false, true})
            for (const bool l0 : {false, true})
                for (const bool l1 : {false, true})
                    for (const std::vector<uint32_t>& sel : selections) {
                        groups[0].locked = l0;
                        groups[1].locked = l1;
                        const bool in0 = std::find(sel.begin(), sel.end(), 10u) != sel.end() || std::find(sel.begin(), sel.end(), 11u) != sel.end();
                        const bool in1 = std::find(sel.begin(), sel.end(), 20u) != sel.end();
                        const bool any = on && (in0 || in1);
                        const bool all = any && (!in0 || l0) && (!in1 || l1);
                        const FleetDisciplineStep s = fleet_discipline(on, groups, sel, tight);
                        const FleetDiscipline shown = all ? FleetDiscipline::Locked : tight ? FleetDiscipline::Tight : FleetDiscipline::Loose;
                        CHECK(s.shown == shown && s.lock_available == any && s.selection_locked == all);
                        if (shown == FleetDiscipline::Loose) CHECK(s.next == FleetDiscipline::Tight && !s.lock && s.cohesion == true);
                        if (shown == FleetDiscipline::Tight && any) CHECK(s.next == FleetDiscipline::Locked && s.lock == true && !s.cohesion);
                        if (shown == FleetDiscipline::Tight && !any) CHECK(s.next == FleetDiscipline::Loose && !s.lock && s.cohesion == false);
                        if (shown == FleetDiscipline::Locked) CHECK(s.next == FleetDiscipline::Loose && s.lock == false && s.cohesion == false);
                        ++cases;
                    }
    CHECK(cases == 80);
    // Spot checks: a mixed selection (one locked group, one free) is not Locked; fleet groups off never is.
    groups[0].locked = true;
    groups[1].locked = false;
    const std::vector<uint32_t> mixed{10, 20};
    CHECK(fleet_discipline(true, groups, mixed, true).shown == FleetDiscipline::Tight && fleet_discipline(true, groups, mixed, true).lock == true);
    groups[1].locked = true;
    CHECK(fleet_discipline(true, groups, mixed, false).shown == FleetDiscipline::Locked &&
          fleet_discipline(false, groups, mixed, false).shown == FleetDiscipline::Loose);
}

// The control in the deck: shown in space only, 2 steps with fleet groups off and 3 on, New group and Lock only on; each
// click is the Classic key's order (O's cohesion as the FleetCohesion action, Y's SetLock, N's Create).
void test_discipline_control() {
    Huds h;
    Selection s;
    TacticalHudFrame f = frame();
    f.world.local_player = 0;
    std::vector<HudFleetGroup> groups(1);
    groups[0].id = 1;
    groups[0].members = {20, 21};
    f.fleet_groups = groups;
    // Land (no cohesion feed): no control.
    h.modern.build(f, s.units, s.types);
    const ModernHudStats& st = h.modern.stats();
    CHECK(!st.discipline_drawn && st.discipline_lights == 0 && st.fleet_buttons == 0);
    f.cohesion_text = U"Fleet cohesion: Loose (O)";
    h.modern.build(f, s.units, s.types);
    CHECK(st.discipline_drawn && st.discipline_lights == 2 && st.discipline == FleetDiscipline::Loose && st.fleet_buttons == 0 &&
          has_hit(h.modern.hit_boxes(), kModernDisciplineHit) && !has_hit(h.modern.hit_boxes(), kModernLockHit));
    auto click = [&](int32_t id) {
        std::pair<std::vector<commands::EventData>, std::vector<HudAction>> o;
        h.modern.update_input(click_on(id), false, false, o.first, &o.second);
        return o;
    };
    // Loose -> Tight: the cohesion (the O key toggles the same preference).
    auto [c0, a0] = click(kModernDisciplineHit);
    CHECK(c0.empty() && a0.size() == 1 && a0[0].kind == HudAction::Kind::FleetCohesion && a0[0].value == 1);
    // Tight with fleet groups off -> Loose.
    f.cohesion_tight = true;
    h.modern.build(f, s.units, s.types);
    auto [c1, a1] = click(kModernDisciplineHit);
    CHECK(st.discipline == FleetDiscipline::Tight && c1.empty() && a1.size() == 1 && a1[0].value == 0);
    // Fleet groups on: 3 steps and the buttons; Tight -> Locked = Y's SetLock(true).
    f.fleet_groups_on = true;
    h.modern.build(f, s.units, s.types);
    CHECK(st.discipline_lights == 3 && st.fleet_buttons == 2 && st.discipline == FleetDiscipline::Tight);
    const std::vector<uint32_t> sel = {20, 21, 22};
    auto [c2, a2] = click(kModernDisciplineHit);
    CHECK(a2.empty() && c2 == key_commands(hk::kFleetLock, true, false, sel) && c2.size() == 1 && c2[0] == fleet_lock_event(true));
    // The Lock and New group buttons = Y and N.
    CHECK(click(kModernLockHit).first == key_commands(hk::kFleetLock, true, false, sel));
    CHECK(click(kModernNewGroupHit).first == key_commands(hk::kFleetCreate, true, false, sel));
    // Locked -> Loose: SetLock(false) and the cohesion Loose, in two orders; Lock now unlocks (Y on a locked selection).
    groups[0].locked = true;
    h.modern.build(f, s.units, s.types);
    CHECK(st.discipline == FleetDiscipline::Locked);
    auto [c3, a3] = click(kModernDisciplineHit);
    CHECK(c3.size() == 1 && c3[0] == fleet_lock_event(false) && a3.size() == 1 && a3[0].kind == HudAction::Kind::FleetCohesion && a3[0].value == 0);
    CHECK(click(kModernLockHit).first == key_commands(hk::kFleetLock, true, true, sel));
    // Hovered: the one-line-per-step hint.
    RoutedInput hover;
    hover.hover_component = kModernDisciplineHit;
    std::vector<commands::EventData> none;
    h.modern.update_input(hover, false, false, none);
    h.modern.build(f, s.units, s.types);
    CHECK(st.discipline_hint);
}

// #601 the fleet roster: wing cards, group cards in feed order and "+ New group"; the cards' clicks are Classic's wing
// button, group tab (select, Shift adds, right renames) and N; a double click on a group card also centres the camera;
// the header collapses the roster; with fleet groups off only the wings.
void test_roster() {
    Huds h;
    Selection s;
    TacticalHudFrame f = frame();
    f.world.local_player = 0;
    std::vector<HudWing> wings(1);
    wings[0].label = U"FIGHTERS";
    wings[0].hotkey = U"F2";
    wings[0].squadrons = 2;
    wings[0].ids = {40, 41};
    wings[0].craft_alive = 2;
    wings[0].craft_total = 6;
    f.wings = wings;
    std::vector<HudFleetGroup> fleets(2);
    fleets[0].id = 1;
    fleets[0].members = {20, 21};
    fleets[1].id = 2;
    fleets[1].name = U"Escort";
    fleets[1].locked = true;
    fleets[1].members = {50};  // not selected
    f.fleet_groups_on = true;
    f.fleet_groups = fleets;
    h.build(f, s.units, s.types);
    const ModernHudStats& st = h.modern.stats();
    CHECK(st.roster && !st.roster_collapsed && st.wings == 1 && st.roster_groups == 2 && st.roster_new && st.roster_selected == 1);
    CHECK(has_hit(h.modern.hit_boxes(), kWingHitBase) && has_hit(h.modern.hit_boxes(), kModernRosterGroupBase + 1) &&
          has_hit(h.modern.hit_boxes(), kModernRosterNewHit));
    // Clicks: the wing card = Classic's wing button; the first group card = Classic's tab of that group (its only tab).
    size_t n = 0;
    CHECK(h.same_click(kWingHitBase, kWingHitBase, false, false, Button::Left, &n) && n == 1);
    for (const bool shift : {false, true}) {
        f.time += 1.0;  // never a double click
        h.build(f, s.units, s.types);
        CHECK(h.same_click(kFleetTabHitBase, kModernRosterGroupBase, shift, false, Button::Left, &n) && n == 1);
    }
    {  // right: Classic's rename of the group (what the host reads: the group id and name; the component is the card's)
        std::vector<commands::EventData> cc, mc;
        std::vector<HudAction> ca, ma;
        h.classic.update_input(click_on(kFleetTabHitBase, Button::Right), false, false, 1.0, cc, &ca);
        h.modern.update_input(click_on(kModernRosterGroupBase + 1, Button::Right), false, false, mc, &ma);
        CHECK(ca.size() == 1 && ma.size() == 1 && ma[0].kind == ca[0].kind && ma[0].kind == HudAction::Kind::FleetGroupRename &&
              ma[0].value == 2 && ma[0].name == "Escort" && ca[0].value == 1);
    }
    // A second left click within 0.5 s: the selection and the camera on the group (the members' mean position).
    f.time += 1.0;
    h.build(f, s.units, s.types);
    std::vector<commands::EventData> cmds;
    std::vector<HudAction> acts;
    h.modern.update_input(click_on(kModernRosterGroupBase), false, false, cmds, &acts);
    h.modern.update_input(click_on(kModernRosterGroupBase), false, false, cmds, &acts);
    CHECK(cmds.size() == 2 && acts.size() == 1 && acts[0].kind == HudAction::Kind::RadarCameraJump);
    // "+ New group" = N.
    const std::vector<uint32_t> sel = {20, 21, 22};
    cmds.clear();
    h.modern.update_input(click_on(kModernRosterNewHit), false, false, cmds, nullptr);
    CHECK(cmds == key_commands(hk::kFleetCreate, true, false, sel) && cmds.size() == 1);
    // Collapse and expand.
    h.modern.update_input(click_on(kModernRosterHeaderHit), false, false, cmds, nullptr);
    h.build(f, s.units, s.types);
    CHECK(st.roster_collapsed && st.wings == 0 && st.roster_groups == 0 && !has_hit(h.modern.hit_boxes(), kWingHitBase));
    h.modern.update_input(click_on(kModernRosterHeaderHit), false, false, cmds, nullptr);
    h.build(f, s.units, s.types);
    CHECK(!st.roster_collapsed && st.wings == 1);
    // Many groups: the cards shrink, then a "+N more" line; every card stays above the map (left end) and the deck.
    std::vector<HudFleetGroup> many(30);
    for (size_t k = 0; k < many.size(); ++k) {
        many[k].id = static_cast<uint16_t>(k + 1);
        many[k].members = {static_cast<uint32_t>(100 + k)};
    }
    f.fleet_groups = many;
    h.build(f, s.units, s.types);
    CHECK(st.roster_groups + st.roster_more == 30 && st.roster_more > 0 && st.roster_new);
    float map_top = -1e9f, lowest = 1e9f;
    for (const HudHitBox& b : h.modern.hit_boxes()) {
        if (b.component == h.t.find("radar") || b.component == kModernMapHit) map_top = std::max(map_top, b.max.y);
        if ((b.component >= kModernRosterGroupBase && b.component < kModernRosterGroupBase + 64) || b.component == kModernRosterNewHit ||
            b.component == kWingHitBase)
            lowest = std::min(lowest, b.min.y);
    }
    CHECK(map_top > -1e9f && lowest >= map_top);
    f.fleet_groups = fleets;
    // Fleet groups off: the wings only.
    f.fleet_groups_on = false;
    h.build(f, s.units, s.types);
    CHECK(st.wings == 1 && st.roster_groups == 0 && !st.roster_new);
}

// #602 group badges: one per selected own unit beside its world icon (a squadron's craft share their one icon), one per
// group and wing on the map, an outline tag for each selected locked group; nothing without groups or wings. Classic's
// outline (the shared fleet_outline) is unchanged.
void test_badges() {
    Huds h;
    Selection s;
    TacticalHudFrame f = frame();
    f.world.local_player = 0;
    CameraParams cp;
    cp.eye = {0.0f, -600.0f, 600.0f};
    cp.target = {0.0f, 0.0f, 0.0f};
    f.world.view = make_view(cp, 1920, 1080);
    std::vector<HudUnit> units = s.units;
    for (size_t i = 0; i < units.size(); ++i) units[i].position = {40.0f * static_cast<float>(i), 0.0f, 0.0f};
    std::vector<HudFleetGroup> fleets(2);
    fleets[0].id = 1;
    fleets[0].members = {20, 21};
    fleets[1].id = 2;
    fleets[1].name = U"Escort";
    fleets[1].locked = true;
    fleets[1].members = {22};
    f.fleet_groups_on = true;
    f.fleet_groups = fleets;
    h.build(f, units, s.types);
    const ModernHudStats& st = h.modern.stats();
    CHECK(st.world_badges == 3 && st.radar_badges == 2 && st.outline_tags == 1);
    CHECK(h.classic.stats().fleet_outlines == 1);
    const std::optional<FleetOutline> o = fleet_outline(fleets[1], units, s.types);
    CHECK(o && std::fabs(o->centre.x - 80.0f) < 1e-3f && o->radius >= 12.0f);
    // A squadron craft without the icon: no badge of its own.
    units[1].squadron_member = true;
    h.modern.build(f, units, s.types);
    CHECK(st.world_badges == 2);
    // Unlocked: no tag; fleet groups off and no wing: nothing.
    fleets[1].locked = false;
    h.modern.build(f, units, s.types);
    CHECK(st.outline_tags == 0);
    f.fleet_groups_on = false;
    h.modern.build(f, units, s.types);
    CHECK(st.world_badges == 0 && st.radar_badges == 0 && st.outline_tags == 0);
}

// #603 lagging stragglers: the marks on the roster card, the run tab, the world badge and the outline show only while the
// feed reports lagging members; the move readout only while a locked move is aimed, with "N lagging" while one runs.
// Classic draws the same with or without the lagging feed.
void test_lagging() {
    Huds h;
    Selection s;
    TacticalHudFrame f = frame();
    f.world.local_player = 0;
    CameraParams cp;
    cp.eye = {0.0f, -600.0f, 600.0f};
    cp.target = {0.0f, 0.0f, 0.0f};
    f.world.view = make_view(cp, 1920, 1080);
    std::vector<HudUnit> units = s.units;
    for (size_t i = 0; i < units.size(); ++i) units[i].position = {40.0f * static_cast<float>(i), 0.0f, 0.0f};
    std::vector<HudFleetGroup> fleets(1);
    fleets[0].id = 1;
    fleets[0].name = U"Line";
    fleets[0].locked = true;
    fleets[0].members = {20, 21, 22};
    f.fleet_groups_on = true;
    f.fleet_groups = fleets;
    h.build(f, units, s.types);
    const ModernHudStats& st = h.modern.stats();
    const uint64_t classic_before = h.classic.draw_list().digest();
    CHECK(st.roster_groups == 1 && st.chips_locked == 1 && st.outline_tags == 1);
    CHECK(st.roster_lagging == 0 && st.tab_lagging == 0 && st.badges_lagging == 0 && st.outline_lagging == 0 && !st.move_readout);
    // A running move waits for 22.
    fleets[0].lagging = {22};
    h.build(f, units, s.types);
    CHECK(st.roster_lagging == 1 && st.tab_lagging == 1 && st.badges_lagging == 1 && st.outline_lagging == 1);
    CHECK(st.world_badges == 3 && !st.move_readout);
    CHECK(h.classic.draw_list().digest() == classic_before);
    // Aiming a locked move: the readout, with "1 lagging" while the move runs.
    const std::vector<HudFleetGoal> goals = {{20, 400.0f, 200.0f}, {21, 440.0f, 200.0f}, {22, 480.0f, 200.0f}};
    f.fleet_preview = goals;
    h.modern.build(f, units, s.types);
    CHECK(st.move_readout && st.move_readout_lagging == 1);
    fleets[0].lagging.clear();
    h.modern.build(f, units, s.types);
    CHECK(st.move_readout && st.move_readout_lagging == 0 && st.roster_lagging == 0 && st.tab_lagging == 0);
    // Targeting (no preview there) or fleet groups off: no readout.
    f.world.targeting_active = true;
    h.modern.build(f, units, s.types);
    CHECK(!st.move_readout);
    f.world.targeting_active = false;
    f.fleet_groups_on = false;
    fleets[0].lagging = {22};
    h.modern.build(f, units, s.types);
    CHECK(!st.move_readout && st.badges_lagging == 0 && st.roster_lagging == 0);
}

// ---- #593 the galactic map ----

const char* kGalacticXml = R"(<?xml version="1.0"?>
<CommandBarComponents>
  <CommandBarComponent Name="Text_Credits"><Type>TextButton</Type><Text_Color>255 212 33 255</Text_Color></CommandBarComponent>
  <CommandBarComponent Name="b_option_g"><Type>TextButton</Type><Icon_Texture_Name>i_button_units.tga</Icon_Texture_Name></CommandBarComponent>
  <CommandBarComponent Name="b_play_pause"><Type>Button</Type><Blank_Texture_Name>i_button_units.tga</Blank_Texture_Name></CommandBarComponent>
  <CommandBarComponent Name="b_fast_forward"><Type>Button</Type><Blank_Texture_Name>i_button_units.tga</Blank_Texture_Name></CommandBarComponent>
  <CommandBarComponent Name="b_story_arc_g"><Type>Button</Type><Blank_Texture_Name>i_button_units.tga</Blank_Texture_Name></CommandBarComponent>
  <CommandBarComponent Name="planet_info"><Type>TextButton</Type><Icon_Texture_Name>i_button_units.tga</Icon_Texture_Name></CommandBarComponent>
  <CommandBarComponent Name="advanced_planetary"><Type>Button</Type><Blank_Texture_Name>i_button_units.tga</Blank_Texture_Name></CommandBarComponent>
  <CommandBarComponent Name="b_Create_00"><Type>TextButton</Type><Blank_Texture_Name>i_button_units.tga</Blank_Texture_Name></CommandBarComponent>
  <CommandBarComponent Name="b_Create_01"><Type>TextButton</Type><Blank_Texture_Name>i_button_units.tga</Blank_Texture_Name></CommandBarComponent>
  <CommandBarComponent Name="b_Create_02"><Type>TextButton</Type><Blank_Texture_Name>i_button_units.tga</Blank_Texture_Name></CommandBarComponent>
  <CommandBarComponent Name="b_Create_03"><Type>TextButton</Type><Blank_Texture_Name>i_button_units.tga</Blank_Texture_Name></CommandBarComponent>
  <CommandBarComponent Name="queue00"><Type>Button</Type><Blank_Texture_Name>i_button_units.tga</Blank_Texture_Name></CommandBarComponent>
  <CommandBarComponent Name="queue01"><Type>Button</Type><Blank_Texture_Name>i_button_units.tga</Blank_Texture_Name></CommandBarComponent>
  <CommandBarComponent Name="q1_fleet"><Type>Button</Type><Blank_Texture_Name>i_button_units.tga</Blank_Texture_Name></CommandBarComponent>
  <CommandBarComponent Name="q1_0000"><Type>Button</Type><Blank_Texture_Name>i_button_units.tga</Blank_Texture_Name></CommandBarComponent>
  <CommandBarComponent Name="radar_galactic"><Type>Button</Type><Size>175 175</Size></CommandBarComponent>
</CommandBarComponents>
)";

struct GalacticHuds {
    ComponentTable t;
    TextureAtlas atlas;
    std::unique_ptr<GlyphRasterizer> raster = make_box_rasterizer();
    FontCache fonts{*raster};
    GalacticHud classic;
    ModernGalacticHud modern;
    std::vector<GalacticPlanetInfo> planets;
    std::vector<GalacticBuildOption> options;
    std::vector<HudQueueEntry> queue;
    std::vector<GalacticFleet> fleets;
    std::vector<HeroEntry> heroes;
    std::vector<HudAlert> alerts;
    GalacticHudFrame f;
    double now = 1.0;
    GalacticHuds() {
        const data::XmlDocument doc = data::XmlDocument::parse(std::string_view(kGalacticXml), "modern_galactic.xml");
        if (doc.ok()) t.parse(doc.root());
        atlas.texture_width = atlas.texture_height = 256;
        atlas.add({"i_button_units.tga", 0, 0, 56, 56, true});
        atlas.add({"i_unit.tga", 64, 0, 32, 32, true});
        classic.init(t, atlas, fonts);
        classic.resize(1920, 1080);
        modern.init(t, atlas, fonts);
        modern.resize(1920, 1080);
        planets.resize(2);
        for (size_t i = 0; i < planets.size(); ++i) {
            planets[i].planet = 100 + static_cast<uint32_t>(i);
            planets[i].position = {-200.0f + 400.0f * static_cast<float>(i), 0.0f, 0.0f};
            planets[i].name = i == 0 ? U"Coruscant" : U"Kuat";
        }
        planets[1].selected = true;
        planets[1].income = 120;
        planets[1].owner_color = {60, 120, 255, 255};
        // b_Create_00 / _02 on Classic's land tab (the start): a ground unit and a greyed one; a space unit on the other tab.
        options.resize(3);
        options[0].type_index = 5;
        options[0].icon = "i_unit.tga";
        options[0].cost = 300;
        options[1].type_index = 6;
        options[1].enabled = false;
        options[1].block = HudBuildOption::Block::PopCap;
        options[2].type_index = 7;
        options[2].space = true;
        options[2].cost = 1200;
        HudQueueEntry q;
        q.type_index = 7;
        q.location = 101;
        q.progress = 0.4f;
        queue.push_back(q);
        GalacticFleet own;
        own.fleet = 900;
        own.slot = 0;
        own.units = {{901, "i_unit.tga", 2, false}, {902, "i_unit.tga", 1, false}};
        GalacticFleet enemy;
        enemy.fleet = 950;
        enemy.enemy = true;
        enemy.units = {{951, "i_unit.tga", 1, false}};
        fleets = {own, enemy};
        // The hero bar's roster: one at Kuat, one in hyperspace, one in production.
        heroes.resize(3);
        heroes[0].object = 700;
        heroes[0].icon = "i_unit.tga";
        heroes[0].planet = 101;
        heroes[0].tooltip = U"Hero";
        heroes[1].object = 701;
        heroes[1].tooltip = U"Traveller";
        heroes[2].object = 0;
        heroes[2].type_name = "Builder_Hero";
        HudAlert a;
        a.kind = HudAlert::Kind::Radar;
        a.text = U"Enemy fleet approaching Kuat";
        a.has_position = true;
        a.x = 200.0f;
        a.y = 0.0f;
        alerts = {a};
        GalacticCameraController cam;
        f.view = make_view(cam.params(16.0f / 9.0f), 1920, 1080);
        f.planets = planets;
        f.credits = 12345;
        f.income = 1250;
        f.pop = 13;
        f.pop_cap = 40;
        f.tech = 2;
        f.day = 7;
        f.planets_owned = 5;
        f.heroes = heroes;
        f.alerts = alerts;
        f.providers.build_options = [this](uint32_t, std::vector<GalacticBuildOption>& out) { out = options; };
        f.providers.build_queue = [this](std::vector<HudQueueEntry>& out) { out = queue; };
        f.providers.fleets = [this](uint32_t, std::vector<GalacticFleet>& out) { out = fleets; };
    }
    void build() {
        classic.build(f);
        modern.build(f);
    }
    struct Out {
        std::vector<commands::EventData> cmds;
        std::vector<HudAction> acts;
    };
    Out classic_click(const char* name, Button b = Button::Left) {
        Out o;
        now += 1.0;  // never a double click
        classic.update_input(click_on(t.find(name), b), false, false, now, o.cmds, &o.acts);
        return o;
    }
    Out modern_click(int32_t id, Button b = Button::Left) {
        Out o;
        now += 1.0;
        modern.update_input(click_on(id, b), false, false, now, o.cmds, &o.acts);
        return o;
    }
};

// What the host reads from a galactic action (component and name differ between the styles for the Modern-only tiles;
// a Button action's name is its meaning, so it must match).
bool same_galactic(const GalacticHuds::Out& a, const GalacticHuds::Out& b) {
    if (a.cmds != b.cmds || a.acts.size() != b.acts.size()) {
        std::printf("  galactic click: classic %zu cmds %zu acts, modern %zu cmds %zu acts\n", a.cmds.size(), a.acts.size(), b.cmds.size(),
                    b.acts.size());
        return false;
    }
    for (size_t i = 0; i < a.acts.size(); ++i) {
        const HudAction &x = a.acts[i], &y = b.acts[i];
        if (x.kind != y.kind || x.button != y.button || x.shift != y.shift || x.ctrl != y.ctrl || x.value != y.value ||
            x.unit != y.unit || x.ids != y.ids || x.x != y.x || x.y != y.y || (x.kind == HudAction::Kind::Button && x.name != y.name))
            return false;
    }
    return true;
}

// The status corner, the controls and the Classic overlay: with it the Classic HUD draws no command bar, organiser or
// radar (no hit boxes there), and the Modern view nothing while the planet map is open.
void test_galactic_view() {
    GalacticHuds g;
    g.classic.set_modern_overlay(true);
    g.build();
    const ModernGalacticStats& st = g.modern.stats();
    CHECK(st.status_corner && st.credits_text == hud_credits_text(12345) && st.income_text == U"+1,250 / day" &&
          st.pop_text == hud_pop_text(13, 40) && st.tech_text == hud_tech_text(nullptr, 2) && st.planets_text == U"5" && st.day_text == U"7");
    CHECK(st.controls == 4 && st.style_switch);
    CHECK(st.alert_text == U"Enemy fleet approaching Kuat" && st.jump && st.heroes == 3);
    // The battle prompt takes the top centre: no strip, no Jump.
    g.f.battle.active = true;
    g.modern.build(g.f);
    CHECK(st.alert_text.empty() && !st.jump);
    std::vector<HudAction> none;
    CHECK(!g.modern.jump(none));
    g.f.battle.active = false;
    g.modern.build(g.f);
    CHECK(st.deck && st.planet_card && st.planet_name == U"KUAT" && st.fleets == 2 && st.enemy_fleets == 1 && st.queue_entries == 1);
    for (const char* name : {"b_Create_00", "queue00", "q1_fleet", "b_option_g", "b_play_pause", "radar_galactic", "Text_Credits"})
        CHECK(!has_hit(g.classic.hit_boxes(), g.t.find(name)));
    CHECK(g.classic.stats().build_buttons == 0 && g.classic.stats().queue_entries == 0 && !g.classic.stats().organize_shown &&
          g.classic.stats().radar_blips == 0 && g.classic.selected_planet() == 1);
    // Off: the Classic HUD as before.
    g.classic.set_modern_overlay(false);
    g.classic.build(g.f);
    CHECK(g.classic.stats().build_buttons == 2 && g.classic.stats().queue_entries == 1 && has_hit(g.classic.hit_boxes(), g.t.find("b_option_g")));
    // The advanced planet map: Classic whole (overlay or not), Modern nothing.
    g.f.advanced_map_open = true;
    g.classic.set_modern_overlay(true);
    g.build();
    CHECK(!g.modern.stats().deck && !g.modern.stats().status_corner && g.modern.hit_boxes().empty());
    // No planet selected: no deck, the corners stay.
    g.f.advanced_map_open = false;
    g.planets[1].selected = false;
    g.build();
    CHECK(!g.modern.stats().deck && g.modern.stats().status_corner && g.modern.selected_planet() == -1);
}

// Parity: selecting a fleet, buying, a refused buy, cancelling (right click and a queue entry), pause and menu emit the
// Classic commands and actions; Jump and a hero row move the camera (RadarCameraJump); the tabs sort the options.
void test_galactic_parity() {
    GalacticHuds g;
    g.modern.set_build_tab(GalacticBuildTab::Ground);  // Classic starts on its land tab
    g.build();
    const ModernGalacticStats& st = g.modern.stats();
    CHECK(st.tabs_live == 2 && st.build_options == 2 && st.build_blocked == 1 && st.build_texts.size() == 2 &&
          st.build_texts[0] == U"300" && st.build_texts[1] == U"Population cap");
    using Out = GalacticHuds::Out;
    for (const Button b : {Button::Left, Button::Right}) {
        CHECK(same_galactic(g.classic_click("b_Create_00", b), g.modern_click(kModernGalacticBuildBase + 0, b)));
        CHECK(same_galactic(g.classic_click("b_Create_02", b), g.modern_click(kModernGalacticBuildBase + 1, b)));
    }
    const Out buy = g.modern_click(kModernGalacticBuildBase + 0);
    CHECK(buy.cmds.size() == 1 && buy.cmds[0] == commands::ev::production(5, 101));
    const Out refused = g.modern_click(kModernGalacticBuildBase + 1);
    CHECK(refused.cmds.empty() && refused.acts.size() == 1 && refused.acts[0].kind == HudAction::Kind::BuildRefused &&
          refused.acts[0].value == static_cast<int32_t>(HudBuildOption::Block::PopCap) && refused.acts[0].unit == 6);
    const Out cancel = g.modern_click(kModernGalacticQueueBase + 0);
    CHECK(cancel.cmds.size() == 1 && cancel.cmds[0] == commands::ev::production(7, 101, true, 0, 0));
    CHECK(same_galactic(g.classic_click("queue00"), cancel));
    const Out select = g.modern_click(kModernGalacticFleetBase + 0);
    CHECK(select.acts.size() == 1 && select.acts[0].kind == HudAction::Kind::GalacticSelect && select.acts[0].ids == std::vector<uint32_t>{900});
    CHECK(same_galactic(g.classic_click("q1_fleet"), select));
    const Out enemy = g.modern_click(kModernGalacticFleetBase + 1);
    CHECK(enemy.cmds.empty() && enemy.acts.empty());
    for (const char* name : {"b_play_pause", "b_option_g", "b_fast_forward"}) {
        const Out c = g.classic_click(name), m = g.modern_click(g.t.find(name));
        CHECK(same_galactic(c, m) && c.cmds.size() + c.acts.size() == 1);
    }
    // Jump (and its hotkey): the camera there. A hero row: the hero bar's click (HeroFocusPlanet), nothing in hyperspace
    // or in production.
    const Out jump = g.modern_click(kModernJumpHit);
    CHECK(jump.acts.size() == 1 && jump.acts[0].kind == HudAction::Kind::RadarCameraJump && jump.acts[0].x == 200.0f && jump.acts[0].y == 0.0f);
    std::vector<HudAction> key;
    CHECK(g.modern.jump(key) && key.size() == 1 && key[0].x == 200.0f);
    const Out hero = g.modern_click(kModernGalacticHeroBase + 0);
    CHECK(hero.acts.size() == 1 && hero.acts[0].kind == HudAction::Kind::HeroFocusPlanet && hero.acts[0].unit == 700 &&
          hero.acts[0].ids == std::vector<uint32_t>{101});
    for (const int32_t row : {1, 2}) {
        const Out none = g.modern_click(kModernGalacticHeroBase + row);
        CHECK(none.cmds.empty() && none.acts.empty());
    }
    {  // the bar's double click: a second click within 0.5 s adds HeroZoomPlanet
        std::vector<commands::EventData> c;
        std::vector<HudAction> a;
        g.modern.update_input(click_on(kModernGalacticHeroBase + 0), false, false, 100.0, c, &a);
        g.modern.update_input(click_on(kModernGalacticHeroBase + 0), false, false, 100.2, c, &a);
        CHECK(a.size() == 3 && a[1].kind == HudAction::Kind::HeroFocusPlanet && a[2].kind == HudAction::Kind::HeroZoomPlanet &&
              a[2].ids == std::vector<uint32_t>{101});
    }
    CHECK(hero_focus_planet(g.heroes[0]) == 101 && hero_focus_planet(g.heroes[1]) == 0 && hero_focus_planet(g.heroes[2]) == 0);
    // In the planet close-up: Planet Info (Classic's PlanetInfo of the selected planet) and Base Layout (its Button action).
    g.f.organize_open = true;
    g.build();
    CHECK(st.planet_buttons == 2);
    const Out info = g.modern_click(g.t.find("planet_info"));
    CHECK(info.acts.size() == 1 && info.acts[0].kind == HudAction::Kind::PlanetInfo && info.acts[0].unit == 101);
    const Out layout = g.modern_click(g.t.find("advanced_planetary"));
    CHECK(layout.acts.size() == 1 && layout.acts[0].kind == HudAction::Kind::Button && layout.acts[0].name == "advanced_planetary");
    // Tabs: an empty tab takes no click; Space lists the space unit; an inaccessible surface leaves Ground empty.
    g.modern_click(kModernGalacticTabBase + static_cast<int32_t>(GalacticBuildTab::Buildings));
    CHECK(g.modern.build_tab() == GalacticBuildTab::Ground);
    g.modern_click(kModernGalacticTabBase + static_cast<int32_t>(GalacticBuildTab::Space));
    g.build();
    CHECK(g.modern.build_tab() == GalacticBuildTab::Space && st.build_options == 1 && st.build_texts[0] == U"1200");
    g.f.land_tab_allowed = false;
    g.build();
    CHECK(st.tabs_live == 1);
    g.options[0].hero = true;
    g.options[1].structure = true;
    CHECK(galactic_build_tab(g.options[0]) == GalacticBuildTab::Heroes && galactic_build_tab(g.options[1]) == GalacticBuildTab::Buildings &&
          galactic_build_tab(g.options[2]) == GalacticBuildTab::Space);
    // The style switch asks the host to switch.
    const Out sw = g.modern_click(kModernStyleSwitchHit);
    CHECK(sw.acts.size() == 1 && sw.acts[0].kind == HudAction::Kind::HudStyleToggle);
}

// Switching the style keeps the selection (the host's, read from the frame by both) and Modern's build tab.
void test_galactic_switch() {
    GalacticHuds g;
    g.modern.set_build_tab(GalacticBuildTab::Space);
    for (const bool modern : {true, false, true}) {
        g.classic.set_modern_overlay(modern);
        g.classic.build(g.f);
        if (modern) g.modern.build(g.f);
        CHECK(g.classic.selected_planet() == 1 && (!modern || (g.modern.selected_planet() == 1 && g.modern.build_tab() == GalacticBuildTab::Space)));
        CHECK(g.classic.stats().build_buttons == (modern ? 0u : 2u));
    }
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
    test_map();
    test_land();
    test_hotkey();
    test_world_split();
    test_tab_chips();
    test_discipline_rule();
    test_roster();
    test_badges();
    test_lagging();  // #603
    test_discipline_control();
    test_galactic_view();
    test_galactic_parity();
    test_galactic_switch();
    if (g_fail) {
        std::printf("alamo_ui_hud_modern_tests: %d failure(s)\n", g_fail);
        return 1;
    }
    std::printf("alamo_ui_hud_modern_tests: all passed\n");
    return 0;
}
