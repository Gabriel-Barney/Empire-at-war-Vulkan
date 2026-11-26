// alamo_ui_hud_modern_tests (#584, docs/DECISIONS.md "Modern HUD: exception to the current focus"): the Modern tactical
// HUD style against the Classic one on the HUD seam (frame in; stats, hit boxes, commands and actions out): the status
// corner shows the Classic values, the shared game controls send the Classic commands and actions, the style switch
// asks the host to switch, the style names, the text scale, and the Classic world-space split.
#include <cstdio>
#include <string_view>
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
</CommandBarComponents>)";

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
        modern.init(t, fonts);
        modern.resize(1920, 1080);
    }
    void build(const TacticalHudFrame& f) {
        classic.build(f, {}, {});
        modern.build(f, {}, {});
    }
};

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
    CHECK(hk::kHudStyle == hk::kFleetLock + 1 && hk::kHudStyle + 1 == kHotkeySlots);
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
    test_controls_parity();
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
