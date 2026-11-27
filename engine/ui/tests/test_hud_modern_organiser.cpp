// alamo_ui_hud_modern_organiser_tests (#613, docs/DECISIONS.md "Modern HUD: exception to the current focus"): the Modern
// galactic organiser and map against the Classic fleet organiser and galactic radar on the HUD seam (frame in; commands,
// actions and drag state out). Every organiser drop of #613 - merge, launch, land, detach, move, and the drops that do
// nothing (the same fleet, a land unit into the world, a foreign fleet) - and the organiser clicks (select, rename by
// double click) are driven through both HUDs with the same press / move / release, and must emit the same commands and
// actions; so must a radar click at the same point of the map. Building and driving the Modern view leaves the Classic
// draw list as it was. Synthetic data only.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "alamo/data/xml.hpp"
#include "alamo/test/support.hpp"
#include "alamo/ui/camera.hpp"
#include "alamo/ui/galactic_hud.hpp"
#include "alamo/ui/hud_font.hpp"
#include "alamo/ui/modern_galactic_hud.hpp"
#include "alamo/ui/picking.hpp"

using namespace alamo;
using namespace alamo::ui;
using ::alamo::test::g_fail;

namespace {

constexpr uint32_t kNone = commands::kNoObject;

std::string organiser_xml() {
    std::string x = "<CommandBarComponents>\n";
    auto add = [&](const std::string& name) {
        x += "<CommandBarComponent Name=\"" + name +
             "\"><Type>TextButton</Type><Blank_Texture_Name>i_frame.tga</Blank_Texture_Name><Drag_And_Drop>True</Drag_And_Drop></CommandBarComponent>\n";
    };
    for (int n = 1; n <= 3; ++n) {
        add("q" + std::to_string(n) + "_fleet");
        for (int c = 0; c <= 3; ++c) add("q" + std::to_string(n) + "_000" + std::to_string(c));
    }
    for (int k = 0; k < 3; ++k) add("land_fleet_0" + std::to_string(k));
    add("g_planet_fleet");  // the map fleet buttons (Classic's map layer in both styles)
    x += "<CommandBarComponent Name=\"radar_galactic\"><Type>Button</Type><Size>175 175</Size></CommandBarComponent>\n";
    return x + "</CommandBarComponents>";
}

struct Out {
    std::vector<commands::EventData> cmds;
    std::vector<HudAction> acts;
    bool valid = false;  // the drag's target took the drop (the highlight just before the release)
};

// What the host reads from an organiser action (the component ids and names differ for the Modern tiles; a radar
// action's name is its component's, so it must match).
bool same(const Out& a, const Out& b) {
    if (a.cmds != b.cmds || a.acts.size() != b.acts.size() || a.valid != b.valid) {
        std::printf("  classic %zu cmds %zu acts valid %d, modern %zu cmds %zu acts valid %d\n", a.cmds.size(), a.acts.size(),
                    a.valid ? 1 : 0, b.cmds.size(), b.acts.size(), b.valid ? 1 : 0);
        return false;
    }
    for (size_t i = 0; i < a.acts.size(); ++i) {
        const HudAction &x = a.acts[i], &y = b.acts[i];
        if (x.kind != y.kind || x.button != y.button || x.shift != y.shift || x.ctrl != y.ctrl || x.value != y.value ||
            x.unit != y.unit || x.ids != y.ids || (x.kind == HudAction::Kind::RadarCameraJump && (x.component != y.component || x.name != y.name)))
            return false;
    }
    return true;
}

bool centre_px(const std::vector<HudHitBox>& hits, const HudCanvas& c, int32_t id, Vec2f& px) {
    for (const HudHitBox& h : hits)
        if (h.component == id) {
            px = c.canvas_to_pixel(0.5f * (h.min.x + h.max.x), 0.5f * (h.min.y + h.max.y));
            return true;
        }
    return false;
}

struct Env {
    ComponentTable t;
    TextureAtlas atlas;
    std::unique_ptr<GlyphRasterizer> raster = make_box_rasterizer();
    FontCache fonts{*raster};
    GalacticHud classic;
    ModernGalacticHud modern;
    std::vector<GalacticPlanetInfo> planets;
    std::vector<GalacticFleet> fleets;
    std::vector<GalacticFleetSlot> land;
    GalacticHudFrame f;
    GalacticCameraController cam;
    double now = 10.0;

    Env() {
        const std::string xml = organiser_xml();
        const data::XmlDocument doc = data::XmlDocument::parse(std::string_view(xml), "organiser.xml");
        t.parse(doc.root());
        atlas.texture_width = atlas.texture_height = 128;
        atlas.add({"i_frame.tga", 0, 0, 40, 40, true});
        atlas.add({"i_unit.tga", 64, 0, 30, 30, true});
        classic.init(t, atlas, fonts);
        classic.resize(1024, 768);
        modern.init(t, atlas, fonts);
        modern.resize(1024, 768);
        planets.resize(2);
        planets[0].planet = 100;
        planets[0].position = {200, 100, 0};
        planets[0].selected = true;
        planets[0].name = U"Kuat";
        planets[1].planet = 101;
        planets[1].position = {350, 200, 0};
        planets[1].name = U"Corulag";
        // Own fleet 900 in slot 0 (a stack of two of type 7 and a lone type 8), own fleet 901 in slot 1, an enemy fleet,
        // and two land units.
        GalacticFleet a;
        a.fleet = 900;
        a.slot = 0;
        a.units = {{9001, "i_unit.tga", 1, false, 7}, {9002, "i_unit.tga", 1, false, 7}, {9003, "i_unit.tga", 1, false, 8}};
        GalacticFleet b;
        b.fleet = 901;
        b.slot = 1;
        b.units = {{9011, "i_unit.tga", 1, false, 7}};
        GalacticFleet e;
        e.fleet = 950;
        e.slot = 2;
        e.enemy = true;
        e.units = {{9501, "i_unit.tga", 1, false, 7}};
        fleets = {a, b, e};
        land = {{8001, "i_unit.tga", 1, false}, {8002, "i_unit.tga", 1, false}};
        f.view = make_view(cam.params(4.0f / 3.0f), 1024, 768);
        f.organize_open = true;
        f.providers.fleets = [this](uint32_t, std::vector<GalacticFleet>& out) { out = fleets; };
        f.providers.land_forces = [this](uint32_t, std::vector<GalacticFleetSlot>& out) { out = land; };
        build();
    }
    void build() {
        f.planets = planets;
        classic.build(f);
        modern.set_classic_drag(classic.drag());  // as the host (app_frame.cpp)
        modern.build(f);
    }
    int32_t id(const std::string& name) const { return t.find(name); }
    // The Modern organiser tile of `fleet`'s header (unit 0) or stack starting with `unit`, or of land unit `unit`.
    int32_t pad(uint32_t fleet, uint32_t unit, bool is_land = false) const {
        for (const ModernOrganiserPad& p : modern.organiser_pads())
            if (p.land == is_land && p.fleet == fleet && p.unit == unit) return p.id;
        return -1;
    }
    Vec2f planet_px(size_t i) const {
        float sx = 0, sy = 0;
        CHECK(project(f.view, planets[i].position, sx, sy));
        return {sx, sy};
    }
    // A pixel over no planet (empty space).
    Vec2f space_px() const {
        std::vector<GalacticPlanetScreen> s;
        galactic_planet_screens(f, s);
        for (float y = 40.0f; y < 740.0f; y += 40.0f)
            for (float x = 40.0f; x < 1000.0f; x += 40.0f)
                if (galactic_planet_at(s, x, y) < 0) return {x, y};
        CHECK(false);
        return {};
    }

    // Press `from` at `from_px`, move past the drag threshold, rebuild, move to `to_px` over HUD component `to` (-1: the
    // world), release there; the commands and actions the drop issued and whether the target highlighted.
    template <class Hud>
    Out drag(Hud& hud, int32_t from, Vec2f from_px, int32_t to, Vec2f to_px) {
        Out o;
        RoutedInput r;
        r.world.mouse_x = static_cast<int32_t>(from_px.x);
        r.world.mouse_y = static_cast<int32_t>(from_px.y);
        r.hover_component = from;
        r.owner[static_cast<size_t>(Button::Left)] = PointerTarget::Hud;
        r.hud = {{HudPointerEvent::Kind::Press, Button::Left, from, r.world.mouse_x, r.world.mouse_y}};
        hud.update_input(r, false, false, now, o.cmds, &o.acts);
        r.hud.clear();
        r.world.mouse_x += 10;
        hud.update_input(r, false, false, now + 0.05, o.cmds, &o.acts);
        CHECK(hud.drag().active);
        build();  // the targets shown only while dragging (Classic's empty pads, Modern's New fleet tiles) appear
        Vec2f at = to_px;
        if (to >= 0) centre_px(hud.hit_boxes(), hud.canvas(), to, at);  // this HUD's own target: its centre now
        r.world.mouse_x = static_cast<int32_t>(at.x);
        r.world.mouse_y = static_cast<int32_t>(at.y);
        r.hover_component = to;
        hud.update_input(r, false, false, now + 0.1, o.cmds, &o.acts);
        CHECK(hud.drag().active);
        o.valid = hud.drag().valid;
        r.owner = {};
        r.hud = {{HudPointerEvent::Kind::Release, Button::Left, to, r.world.mouse_x, r.world.mouse_y}};
        if (to == from) r.hud.push_back({HudPointerEvent::Kind::Click, Button::Left, to, r.world.mouse_x, r.world.mouse_y});
        hud.update_input(r, false, false, now + 0.2, o.cmds, &o.acts);
        CHECK(!hud.drag().active);
        now += 1.0;
        build();
        return o;
    }
    Vec2f classic_px(int32_t c) const {
        Vec2f p{-1, -1};
        CHECK(centre_px(classic.hit_boxes(), classic.canvas(), c, p));
        return p;
    }
    Vec2f modern_px(int32_t c) const {
        Vec2f p{-1, -1};
        CHECK(centre_px(modern.hit_boxes(), modern.canvas(), c, p));
        return p;
    }
    // The same drag through both: Classic from / to its components, Modern from / to its tiles (-1: the world at `world_px`).
    Out classic_drag(const std::string& from, const std::string& to, Vec2f world_px = {}) {
        const int32_t fc = id(from), tc = to.empty() ? -1 : id(to);
        return drag(classic, fc, classic_px(fc), tc, world_px);  // a target's pixel: found once the drag shows it
    }
    Out modern_drag(int32_t from, int32_t to, Vec2f world_px = {}) {
        CHECK(from >= 0);
        return drag(modern, from, modern_px(from), to, world_px);
    }
    template <class Hud>
    Out click(Hud& hud, int32_t c, double at, Button b = Button::Left) {
        Out o;
        RoutedInput r;
        r.hover_component = c;
        r.hud = {{HudPointerEvent::Kind::Press, b, c, 0, 0}, {HudPointerEvent::Kind::Release, b, c, 0, 0}, {HudPointerEvent::Kind::Click, b, c, 0, 0}};
        hud.update_input(r, false, false, at, o.cmds, &o.acts);
        return o;
    }
};

// The Modern organiser: a row per own fleet (header and the Classic stacks), the land row; nothing outside the close-up.
void test_organiser_view() {
    Env e;
    const ModernGalacticStats& st = e.modern.stats();
    CHECK(st.organiser && st.organiser_fleets == 2 && st.organiser_stacks == 3 && st.land_pads == 2);
    CHECK(e.classic.stats().organize_shown && e.classic.stats().fleet_slots == 3 && e.classic.stats().land_slots == 2);
    // The stacks are the Classic pads': 9001 + 9002 share one (count 2), 9003 alone.
    CHECK(e.pad(900, 0) >= 0 && e.pad(900, 9001) >= 0 && e.pad(900, 9003) >= 0 && e.pad(900, 9002) < 0 && e.pad(901, 9011) >= 0);
    CHECK(e.pad(0, 8001, true) >= 0 && e.pad(0, 8002, true) >= 0);
    for (const ModernOrganiserPad& p : e.modern.organiser_pads()) CHECK(p.fleet != 950);  // no row for a foreign fleet
    // Outside the planet close-up: no organiser (the deck stays); the map shows with or without a planet selected.
    e.f.organize_open = false;
    e.build();
    CHECK(!st.organiser && e.modern.organiser_pads().empty() && st.deck && st.map && st.map_blips == 2);
    e.planets[0].selected = false;
    e.build();
    CHECK(!st.deck && st.map && st.map_blips == 2);
}

// Every organiser drop of #613 through both HUDs: the same commands, the same highlight.
void test_drop_parity() {
    Env e;
    const Vec2f kuat = e.planet_px(0), corulag = e.planet_px(1), space = e.space_px();
    struct Case {
        const char* what;
        std::string classic_from, classic_to;  // "" = the world
        int32_t modern_from, modern_to;        // -1 = the world
        Vec2f world;
        std::vector<commands::EventData> want;
    };
    const std::vector<Case> cases = {
        {"stack -> other fleet's stack: merge the unit", "q1_0001", "q2_0001", e.pad(900, 9001), e.pad(901, 9011), {},
         {fleet_management_event(commands::kFleetMerge, 901, 900, 9001)}},
        {"stack -> other fleet's header: merge the unit", "q2_0001", "q1_fleet", e.pad(901, 9011), e.pad(900, 0), {},
         {fleet_management_event(commands::kFleetMerge, 900, 901, 9011)}},
        {"header -> other fleet: merge the fleet", "q1_fleet", "q2_fleet", e.pad(900, 0), e.pad(901, 0), {},
         {fleet_management_event(commands::kFleetMerge, 901, 900, kNone)}},
        {"land unit -> fleet: launch", "land_fleet_00", "q1_0002", e.pad(0, 8001, true), e.pad(900, 9003), {},
         {fleet_management_event(commands::kFleetLaunch, 900, kNone, 8001)}},
        {"stack -> land forces: land the unit", "q1_0002", "land_fleet_01", e.pad(900, 9003), e.pad(0, 8002, true), {},
         {fleet_management_event(commands::kFleetLand, 100, 900, 9003)}},
        {"header -> land forces: land the fleet", "q2_fleet", "land_fleet_00", e.pad(901, 0), e.pad(0, 8001, true), {},
         {fleet_management_event(commands::kFleetLand, 100, 901, kNone)}},
        {"stack -> empty space: detach", "q1_0002", "", e.pad(900, 9003), -1, space,
         {fleet_management_event(commands::kFleetDetach, 100, kNone, 9003)}},
        {"stack -> another planet: move the unit", "q1_0002", "", e.pad(900, 9003), -1, corulag,
         {commands::ev::move_object_to_object(9003, 101)}},
        {"header -> another planet: move the fleet", "q1_fleet", "", e.pad(900, 0), -1, corulag, {commands::ev::move_object_to_object(900, 101)}},
        {"header -> its own planet: nothing", "q1_fleet", "", e.pad(900, 0), -1, kuat, {}},
        {"header -> empty space: nothing", "q2_fleet", "", e.pad(901, 0), -1, space, {}},
        {"land unit -> the world: nothing", "land_fleet_00", "", e.pad(0, 8001, true), -1, corulag, {}},
        {"land unit -> land forces: nothing", "land_fleet_00", "land_fleet_01", e.pad(0, 8001, true), e.pad(0, 8002, true), {}, {}},
        {"stack -> its own fleet: nothing", "q1_0001", "q1_0002", e.pad(900, 9001), e.pad(900, 9003), {}, {}},
        {"stack -> its own header: nothing", "q1_0001", "q1_fleet", e.pad(900, 9001), e.pad(900, 0), {}, {}},
    };
    for (const Case& c : cases) {
        const Out co = e.classic_drag(c.classic_from, c.classic_to, c.world);
        const Out mo = e.modern_drag(c.modern_from, c.modern_to, c.world);
        const bool ok = co.cmds == c.want && co.acts.empty() && co.valid == !c.want.empty() && same(co, mo);
        if (!ok) std::printf("  drop: %s\n", c.what);
        CHECK(ok);
    }
    // The Modern land row takes the drop with no land unit on it (it is always drawn); the command is Classic's land.
    e.land.clear();
    e.build();
    CHECK(e.modern.stats().land_pads == 0);
    const Out row = e.modern_drag(e.pad(900, 9003), kModernGalacticLandRowHit);
    CHECK(row.valid && row.cmds.size() == 1 && row.cmds[0] == fleet_management_event(commands::kFleetLand, 100, 900, 9003));
    // A release on any other Modern control (a panel's back) does nothing.
    const Out panel = e.modern_drag(e.pad(900, 9003), kModernPanelHit);
    CHECK(!panel.valid && panel.cmds.empty() && panel.acts.empty());
}

// The Classic empty panes and empty pads: a free orbit slot is the Modern New fleet tile (shown while dragging), a fleet
// row's back is the occupied pane's empty pads (merge into that fleet). Same commands: launch / detach into the slot (op 6
// / detach with b = slot), re-slot the whole fleet (op 8).
void test_slot_parity() {
    Env e;
    CHECK(e.modern.stats().slot_tiles == 0);  // nothing dragged: the row is there, its tiles are not
    const int32_t slot2 = kModernGalacticSlotBase + 2, row900 = kModernGalacticRowBase + 0;
    auto with_b = [](commands::EventData ev, uint32_t b) {
        ev.set("b", b);
        return ev;
    };
    struct Case {
        const char* what;
        std::string classic_from, classic_to;
        int32_t modern_from, modern_to;
        std::vector<commands::EventData> want;
    };
    const std::vector<Case> cases = {
        {"land unit -> free slot 2: launch a new fleet there", "land_fleet_00", "q3_0001", e.pad(0, 8001, true), slot2,
         {with_b(fleet_management_event(commands::kFleetMoveNew, 100, kNone, 8001, 100), 2)}},
        {"fleet unit -> free slot 2: detach into it", "q1_0002", "q3_0001", e.pad(900, 9003), slot2,
         {with_b(fleet_management_event(commands::kFleetDetach, 100, 900, 9003), 2)}},
        {"whole fleet -> free slot 2: re-slot", "q1_fleet", "q3_0002", e.pad(900, 0), slot2,
         {fleet_management_event(commands::kFleetSlot, 100, 900, kNone, kNone, 2)}},
        {"unit -> another fleet's row back (its pane's empty pad): merge", "q2_0001", "q1_0003", e.pad(901, 9011), row900,
         {fleet_management_event(commands::kFleetMerge, 900, 901, 9011)}},
        {"land unit -> a fleet row's back: launch", "land_fleet_01", "q1_0003", e.pad(0, 8002, true), row900,
         {fleet_management_event(commands::kFleetLaunch, 900, kNone, 8002)}},
        {"unit -> its own row's back: nothing", "q1_0001", "q1_0003", e.pad(900, 9001), row900, {}},
    };
    for (const Case& c : cases) {
        const Out co = e.classic_drag(c.classic_from, c.classic_to);
        const Out mo = e.modern_drag(c.modern_from, c.modern_to);
        const bool ok = co.cmds == c.want && co.valid == !c.want.empty() && same(co, mo);
        if (!ok) std::printf("  slot drop: %s\n", c.what);
        CHECK(ok);
    }
    // No own fleet in orbit: every slot is free, and a land unit can still launch (Classic's empty pane q1 = slot 0).
    e.fleets = {e.fleets[2]};  // the enemy fleet only (the local player's orbit: in no pane)
    e.build();
    CHECK(e.modern.stats().organiser_fleets == 0 && e.pad(0, 8001, true) >= 0);
    const Out co = e.classic_drag("land_fleet_00", "q1_0001");
    const Out mo = e.modern_drag(e.pad(0, 8001, true), kModernGalacticSlotBase + 0);
    CHECK(co.cmds.size() == 1 && co.cmds[0] == with_b(fleet_management_event(commands::kFleetMoveNew, 100, kNone, 8001, 100), 0) && same(co, mo));
    // While dragging, all three free slots show.
    RoutedInput r;
    std::vector<commands::EventData> c;
    const Vec2f p = e.modern_px(e.pad(0, 8001, true));
    r.world.mouse_x = static_cast<int32_t>(p.x);
    r.world.mouse_y = static_cast<int32_t>(p.y);
    r.owner[static_cast<size_t>(Button::Left)] = PointerTarget::Hud;
    r.hud = {{HudPointerEvent::Kind::Press, Button::Left, e.pad(0, 8001, true), r.world.mouse_x, r.world.mouse_y}};
    e.modern.update_input(r, false, false, e.now, c);
    r.hud.clear();
    r.world.mouse_x += 10;
    e.modern.update_input(r, false, false, e.now, c);
    e.build();
    CHECK(e.modern.drag().active && e.modern.stats().slot_tiles == 3 && c.empty());
    e.modern.cancel_drag();
}

// A Modern drag never outlives the view: hidden mid-drag (the host's cancel_drag), or the HUD no longer holding the button
// (no release reached this view), it ends without a drop, and a later release sends nothing.
void test_stale_drag() {
    Env e;
    const int32_t from = e.pad(900, 9001);
    const Vec2f p = e.modern_px(from);
    auto start = [&](std::vector<commands::EventData>& c) {
        RoutedInput r;
        r.world.mouse_x = static_cast<int32_t>(p.x);
        r.world.mouse_y = static_cast<int32_t>(p.y);
        r.owner[static_cast<size_t>(Button::Left)] = PointerTarget::Hud;
        r.hud = {{HudPointerEvent::Kind::Press, Button::Left, from, r.world.mouse_x, r.world.mouse_y}};
        e.modern.update_input(r, false, false, e.now, c);
        r.hud.clear();
        r.world.mouse_x += 10;
        e.modern.update_input(r, false, false, e.now, c);
        CHECK(e.modern.drag().active);
    };
    auto release_over_fleet = [&](std::vector<commands::EventData>& c) {
        RoutedInput r;
        r.hover_component = e.pad(901, 9011);
        r.hud = {{HudPointerEvent::Kind::Release, Button::Left, r.hover_component, 0, 0}};
        e.modern.update_input(r, false, false, e.now, c);
    };
    std::vector<commands::EventData> c;
    start(c);
    e.modern.cancel_drag();  // the host: the view hidden (Classic style, the command bar story-hidden)
    CHECK(!e.modern.drag().active);
    release_over_fleet(c);
    CHECK(c.empty());
    // The button let go elsewhere: the next input without a release here ends the drag.
    start(c);
    RoutedInput idle;
    e.modern.update_input(idle, false, false, e.now, c);
    CHECK(!e.modern.drag().active);
    release_over_fleet(c);
    CHECK(c.empty());
    // A press alone (no drag yet) is forgotten the same way: a later move never starts a drag from it.
    RoutedInput press;
    press.owner[static_cast<size_t>(Button::Left)] = PointerTarget::Hud;
    press.hud = {{HudPointerEvent::Kind::Press, Button::Left, from, static_cast<int32_t>(p.x), static_cast<int32_t>(p.y)}};
    e.modern.update_input(press, false, false, e.now, c);
    e.modern.update_input(idle, false, false, e.now, c);
    RoutedInput move;
    move.owner[static_cast<size_t>(Button::Left)] = PointerTarget::Hud;
    move.world.mouse_x = static_cast<int32_t>(p.x) + 30;
    move.world.mouse_y = static_cast<int32_t>(p.y);
    e.modern.update_input(move, false, false, e.now, c);
    CHECK(!e.modern.drag().active && c.empty());
}

// A foreign fleet is never a drop target: Classic draws it inert (its orbit is not the local player's: pane q1), Modern
// gives it no organiser row, and its deck tile (inert) takes no drop.
void test_foreign() {
    Env e;
    e.planets[0].orbit_local = false;
    e.build();
    // Classic: 950 (foreign) -> q1, 901 (slot 1) -> q2, 900 (slot 0) -> q3.
    const Out co = e.classic_drag("q2_0001", "q1_0001");
    const Out mo = e.modern_drag(e.pad(901, 9011), kModernGalacticFleetBase + 2);  // the deck's tiles: own fleets first
    CHECK(co.cmds.empty() && !co.valid && same(co, mo));
    CHECK(e.classic_drag("q2_0001", "q1_fleet").cmds.empty());
    // The own fleets still merge, in either style.
    const Out c2 = e.classic_drag("q2_0001", "q3_0001"), m2 = e.modern_drag(e.pad(901, 9011), e.pad(900, 9001));
    CHECK(c2.cmds.size() == 1 && c2.cmds[0] == fleet_management_event(commands::kFleetMerge, 900, 901, 9011) && same(c2, m2));
}

// The organiser clicks: GalacticSelect of the pad (a stack: all its members), and a double click asks for a rename -
// of the unit (value 0) on a stack, of the fleet (value 1) on a header, of the land unit on its tile.
void test_click_parity() {
    Env e;
    const std::vector<std::pair<std::string, int32_t>> pairs = {
        {"q1_0001", e.pad(900, 9001)}, {"q1_0002", e.pad(900, 9003)}, {"q1_fleet", e.pad(900, 0)}, {"land_fleet_01", e.pad(0, 8002, true)}};
    for (const auto& [name, tile] : pairs) {
        for (const Button b : {Button::Left, Button::Right}) {
            const Out c = e.click(e.classic, e.id(name), e.now, b), m = e.click(e.modern, tile, e.now, b);
            CHECK(c.acts.size() == 1 && c.acts[0].kind == HudAction::Kind::GalacticSelect && same(c, m));
            e.now += 5.0;
        }
        // The double click: select, then select and Rename.
        Out c = e.click(e.classic, e.id(name), e.now), m = e.click(e.modern, tile, e.now);
        const Out c2 = e.click(e.classic, e.id(name), e.now + 0.2), m2 = e.click(e.modern, tile, e.now + 0.2);
        c.acts.insert(c.acts.end(), c2.acts.begin(), c2.acts.end());
        m.acts.insert(m.acts.end(), m2.acts.begin(), m2.acts.end());
        CHECK(c.acts.size() == 3 && c.acts[2].kind == HudAction::Kind::Rename && same(c, m));
        e.now += 5.0;
    }
    const Out stack = e.click(e.modern, e.pad(900, 9001), e.now);
    CHECK(stack.acts.size() == 1 && stack.acts[0].ids == std::vector<uint32_t>({9001, 9002}));
    e.now += 5.0;
    // A drag that ends on the tile it started from is a drop (nothing here), not a click: no select, as Classic.
    const Out back = e.modern_drag(e.pad(900, 9001), e.pad(900, 9001));
    CHECK(back.cmds.empty() && back.acts.empty() && same(e.classic_drag("q1_0001", "q1_0001"), back));
}

// The map: the Classic radar's click at the same point of the map is the same RadarCameraJump (the same component and
// name: the map takes radar_galactic's id); a right click does nothing.
void test_radar_parity() {
    Env e;
    const int32_t radar = e.id("radar_galactic");
    CHECK(e.modern.map_id() == radar && e.modern.stats().map && e.modern.stats().map_view && e.classic.stats().radar_blips == 2);
    auto rect = [](const std::vector<HudHitBox>& hits, int32_t id, Vec2f& mn, Vec2f& mx) {
        for (const HudHitBox& h : hits)
            if (h.component == id) {
                mn = h.min;
                mx = h.max;
                return true;
            }
        return false;
    };
    const GalacticRadarFit fit = galactic_radar_fit(e.planets);
    for (const Vec2f frac : {Vec2f{0.5f, 0.5f}, Vec2f{0.25f, 0.8f}, Vec2f{0.9f, 0.1f}}) {
        Out o[2];
        float world_per_px = 0.0f;
        for (int k = 0; k < 2; ++k) {
            const std::vector<HudHitBox>& hits = k == 0 ? e.classic.hit_boxes() : e.modern.hit_boxes();
            const HudCanvas& canvas = k == 0 ? e.classic.canvas() : e.modern.canvas();
            Vec2f mn{}, mx{};
            CHECK(rect(hits, radar, mn, mx));
            const Vec2f px = canvas.canvas_to_pixel(mn.x + frac.x * (mx.x - mn.x), mn.y + frac.y * (mx.y - mn.y));
            RoutedInput r;
            r.hover_component = radar;
            const auto x = static_cast<int32_t>(px.x), y = static_cast<int32_t>(px.y);
            r.hud = {{HudPointerEvent::Kind::Press, Button::Left, radar, x, y}, {HudPointerEvent::Kind::Release, Button::Left, radar, x, y},
                     {HudPointerEvent::Kind::Click, Button::Left, radar, x, y}};
            if (k == 0) e.classic.update_input(r, false, false, e.now, o[k].cmds, &o[k].acts);
            else e.modern.update_input(r, false, false, e.now, o[k].cmds, &o[k].acts);
            e.now += 5.0;
            // The action is the shared mapping's point under the pixel; a pixel of either radar spans this much galaxy.
            const Vec2f at{0.5f * (mn.x + mx.x), 0.5f * (mn.y + mx.y)}, size{mx.x - mn.x, mx.y - mn.y};
            const Vec2f want = fit.to_world(at, size, canvas.pixel_to_canvas(static_cast<float>(x) + 0.5f, static_cast<float>(y) + 0.5f));
            CHECK(o[k].acts.size() == 1 && o[k].acts[0].kind == HudAction::Kind::RadarCameraJump && o[k].acts[0].x == want.x &&
                  o[k].acts[0].y == want.y && o[k].acts[0].name == "radar_galactic" && o[k].cmds.empty());
            const Vec2f p0 = canvas.pixel_to_canvas(0.0f, 0.0f), p1 = canvas.pixel_to_canvas(1.0f, 1.0f);
            world_per_px = std::max({world_per_px, std::abs(p1.x - p0.x) * 2.0f * fit.half_x / size.x, std::abs(p1.y - p0.y) * 2.0f * fit.half_y / size.y});
        }
        CHECK(same(o[0], o[1]));
        CHECK(o[0].acts.size() == 1 && o[1].acts.size() == 1 && std::abs(o[0].acts[0].x - o[1].acts[0].x) <= 1.5f * world_per_px &&
              std::abs(o[0].acts[0].y - o[1].acts[0].y) <= 1.5f * world_per_px && o[0].acts[0].component == o[1].acts[0].component);
    }
    for (const Button b : {Button::Right, Button::Middle}) {
        const Out c = e.click(e.classic, radar, e.now, b), m = e.click(e.modern, radar, e.now, b);
        CHECK(c.acts.empty() && same(c, m));
    }
    // Without radar_galactic in the table the map has its own id and the same click (no component name).
    ComponentTable bare;
    const data::XmlDocument doc = data::XmlDocument::parse(std::string_view("<CommandBarComponents/>"), "bare.xml");
    bare.parse(doc.root());
    ModernGalacticHud m;
    m.init(bare, e.atlas, e.fonts);
    m.resize(1024, 768);
    m.build(e.f);
    CHECK(m.map_id() == kModernGalacticMapHit && m.stats().map);
    Vec2f px{};
    CHECK(centre_px(m.hit_boxes(), m.canvas(), kModernGalacticMapHit, px));
    const Out o = e.click(m, kModernGalacticMapHit, e.now);
    CHECK(o.acts.size() == 1 && o.acts[0].kind == HudAction::Kind::RadarCameraJump && o.acts[0].name.empty());
}

// The Modern drag's state for the host (cursor, sounds, route preview): a fleet drag over another planet names it
// (the frame's index) and takes the drop; a land unit's drag is fleet_index -1.
void test_drag_state() {
    Env e;
    const int32_t from = e.pad(900, 0);
    const Vec2f p = e.modern_px(from), corulag = e.planet_px(1);
    RoutedInput r;
    std::vector<commands::EventData> c;
    r.world.mouse_x = static_cast<int32_t>(p.x);
    r.world.mouse_y = static_cast<int32_t>(p.y);
    r.hover_component = from;
    r.owner[static_cast<size_t>(Button::Left)] = PointerTarget::Hud;
    r.hud = {{HudPointerEvent::Kind::Press, Button::Left, from, r.world.mouse_x, r.world.mouse_y}};
    e.modern.update_input(r, false, false, e.now, c);
    r.hud.clear();
    r.world.mouse_x += 3;  // within the threshold: no drag yet
    e.modern.update_input(r, false, false, e.now, c);
    CHECK(!e.modern.drag().active);
    r.world.mouse_x = static_cast<int32_t>(corulag.x);
    r.world.mouse_y = static_cast<int32_t>(corulag.y);
    r.hover_component = -1;
    e.modern.update_input(r, false, false, e.now, c);
    const GalacticDrag& d = e.modern.drag();
    CHECK(d.active && d.fleet == 900 && d.unit == 0 && d.fleet_index == 0 && d.over_planet == 1 && d.valid && !d.over_fleet);
    e.build();
    CHECK(e.modern.stats().drop_valid);
    r.hover_component = e.pad(901, 0);
    e.modern.update_input(r, false, false, e.now, c);
    CHECK(d.over_fleet && d.valid && d.over_planet == -1);
    // The release while the HUD no longer owns the button still drops; nothing was sent before it.
    CHECK(c.empty());
    r.owner = {};
    r.hud = {{HudPointerEvent::Kind::Release, Button::Left, r.hover_component, r.world.mouse_x, r.world.mouse_y}};
    e.modern.update_input(r, false, false, e.now, c);
    CHECK(!d.active && c.size() == 1 && c[0] == fleet_management_event(commands::kFleetMerge, 901, 900, kNone));
    // The planet map open: no Modern view, no drag left behind.
    e.f.advanced_map_open = true;
    e.build();
    CHECK(e.modern.organiser_pads().empty() && !e.modern.stats().map && !e.modern.drag().active);
}

// A map fleet dragged from its Classic button (the map layer, Classic in both styles) onto the Modern organiser's rows:
// the Classic HUD asks the Modern view for the target there (set_drop_targets) and sends what a drop on its own
// organiser pads sends - merge into the fleet, land at the selected planet.
void test_map_fleet_onto_rows() {
    Env e;
    GalacticPlanetStack st;
    st.fleet = 900;
    st.slot = 0;
    st.units = 3;
    st.icon = "i_unit.tga";
    e.planets[0].stacks = {st};
    e.build();
    const int32_t button = e.id("g_planet_fleet");
    const Out c_merge = e.drag(e.classic, button, e.classic_px(button), e.id("q2_fleet"), e.classic_px(e.id("q2_fleet")));
    const Out c_land = e.drag(e.classic, button, e.classic_px(button), e.id("land_fleet_00"), e.classic_px(e.id("land_fleet_00")));
    CHECK(c_merge.valid && c_merge.cmds.size() == 1 && c_merge.cmds[0] == fleet_management_event(commands::kFleetMerge, 901, 900, kNone));
    CHECK(c_land.valid && c_land.cmds.size() == 1 && c_land.cmds[0] == fleet_management_event(commands::kFleetLand, 100, 900, kNone));
    const Out c_slot = e.drag(e.classic, button, e.classic_px(button), e.id("q3_0001"), {});  // an empty pad: drawn once dragging
    CHECK(c_slot.valid && c_slot.cmds.size() == 1 && c_slot.cmds[0] == fleet_management_event(commands::kFleetSlot, 100, 900, kNone, kNone, 2));
    // Modern: the Classic HUD draws the map layer only; the rows are Modern's.
    e.classic.set_modern_overlay(true);
    e.build();
    const int32_t row = e.pad(901, 0), land_row = kModernGalacticLandRowHit;
    const Out unset = e.drag(e.classic, button, e.classic_px(button), row, e.modern_px(row));
    CHECK(unset.cmds.empty() && !unset.valid);  // without the hook: as before, nothing
    e.classic.set_drop_targets([&e](int32_t c, GalacticDropTarget& t) { return e.modern.organiser_target(c, t); });
    CHECK(same(c_merge, e.drag(e.classic, button, e.classic_px(button), row, e.modern_px(row))));
    CHECK(same(c_merge, e.drag(e.classic, button, e.classic_px(button), e.pad(901, 9011), e.modern_px(e.pad(901, 9011)))));
    CHECK(same(c_land, e.drag(e.classic, button, e.classic_px(button), land_row, e.modern_px(land_row))));
    // The New fleet tiles show during the Classic drag (set_classic_drag); slot 2's is Classic's empty pane q3.
    {
        RoutedInput r;
        std::vector<commands::EventData> c;
        const Vec2f bp = e.classic_px(button);
        r.world.mouse_x = static_cast<int32_t>(bp.x);
        r.world.mouse_y = static_cast<int32_t>(bp.y);
        r.hover_component = button;
        r.owner[static_cast<size_t>(Button::Left)] = PointerTarget::Hud;
        r.hud = {{HudPointerEvent::Kind::Press, Button::Left, button, r.world.mouse_x, r.world.mouse_y}};
        e.classic.update_input(r, false, false, e.now, c);
        r.hud.clear();
        r.world.mouse_x += 10;
        e.classic.update_input(r, false, false, e.now, c);
        e.build();
        CHECK(e.classic.drag().active && e.modern.stats().slot_tiles == 1 && e.modern.stats().drop_highlights == 0);
        // Over a Modern row whose drop Classic takes: the row is lit (as the host keeps it the Classic hover).
        r.hover_component = row;
        const Vec2f rp = e.modern_px(row);
        r.world.mouse_x = static_cast<int32_t>(rp.x);
        r.world.mouse_y = static_cast<int32_t>(rp.y);
        e.classic.update_input(r, false, false, e.now, c);
        RoutedInput hover;
        hover.hover_component = row;
        hover.owner = r.owner;
        e.modern.update_input(hover, false, false, e.now, c);
        e.build();
        CHECK(e.classic.drag().valid && e.modern.stats().drop_highlights == 1 && e.modern.stats().drop_valid);
        // Over its own row: not taken, not lit.
        r.hover_component = e.pad(900, 0);
        hover.hover_component = r.hover_component;
        e.classic.update_input(r, false, false, e.now, c);
        e.modern.update_input(hover, false, false, e.now, c);
        e.build();
        CHECK(!e.classic.drag().valid && e.modern.stats().drop_highlights == 0);
        r.owner = {};
        r.hud = {{HudPointerEvent::Kind::Release, Button::Left, e.pad(900, 0), r.world.mouse_x, r.world.mouse_y}};
        e.classic.update_input(r, false, false, e.now, c);
        CHECK(c.empty() && !e.classic.drag().active);
        e.now += 1.0;
        e.build();
    }
    CHECK(same(c_slot, e.drag(e.classic, button, e.classic_px(button), kModernGalacticSlotBase + 2, {})));
    // Its own row and any other Modern control take nothing.
    CHECK(e.drag(e.classic, button, e.classic_px(button), e.pad(900, 0), e.modern_px(e.pad(900, 0))).cmds.empty());
    CHECK(e.drag(e.classic, button, e.classic_px(button), kModernPanelHit, e.modern_px(e.pad(901, 0))).cmds.empty());
    GalacticDropTarget t;
    CHECK(!e.modern.organiser_target(kModernPanelHit, t) && !e.modern.organiser_target(-1, t));
}

// Classic unchanged: building and driving the Modern view (its organiser, a drag, its map) leaves the Classic draw list,
// hit boxes and stats as they were.
void test_classic_unchanged() {
    Env e;
    e.classic.build(e.f);
    const uint64_t digest = e.classic.draw_list().digest();
    const size_t hits = e.classic.hit_boxes().size(), quads = e.classic.draw_list().total();
    e.modern_drag(e.pad(900, 9001), e.pad(901, 9011));
    e.click(e.modern, e.modern.map_id(), e.now);
    e.modern.build(e.f);
    e.classic.build(e.f);
    CHECK(e.classic.draw_list().digest() == digest && e.classic.hit_boxes().size() == hits && e.classic.draw_list().total() == quads);
    CHECK(e.classic.stats().organize_shown && e.classic.stats().radar_blips == 2 && e.classic.stats().fleet_slots == 3);
}

}  // namespace

// #669 (the Classic header pad, #515): a fleet's lead shows on its own tile after the header and leaves its type stack;
// a drop there sets the lead with the Classic header-pad drop's commands; a unit that may not lead is refused.
void test_lead_parity() {
    Env e;
    e.fleets[0].units[0].lead = true;  // 9001 leads fleet 900; 9002 (its type) stays a stack of one
    e.fleets[0].units[2].no_lead = true;  // 9003 may not lead
    e.build();
    int32_t lead_tile = -1, empty_tile = -1;
    for (const ModernOrganiserPad& p : e.modern.organiser_pads()) {
        if (p.lead && p.fleet == 900) lead_tile = p.id;
        if (p.lead && p.fleet == 901) empty_tile = p.id;
        if (p.lead && p.fleet == 900) CHECK(p.unit == 9001 && p.leads == std::vector<uint32_t>{9001});
        if (p.lead && p.fleet == 901) CHECK(p.unit == 0 && p.leads.empty());
        if (!p.lead && p.fleet == 900 && p.unit == 9002) CHECK(p.count == 1);  // the lead left the stack: one lower
    }
    CHECK(lead_tile >= 0 && empty_tile >= 0 && e.modern.stats().organiser_leads == 1 && e.modern.stats().organiser_stacks == 3);
    CHECK(e.pad(900, 9001) == lead_tile);  // no stack holds the lead
    struct Case {
        const char* what;
        std::string classic_from, classic_to;
        int32_t modern_from, modern_to;
        std::vector<commands::EventData> want;
    };
    const std::vector<Case> cases = {
        {"another fleet's unit -> the lead pad: merge, clear the old lead, set", "q2_0001", "q1_0000", e.pad(901, 9011), lead_tile,
         {fleet_management_event(commands::kFleetMerge, 900, 901, 9011), commands::ev::set_gui_index(9001, commands::kNoGuiIndex),
          commands::ev::set_gui_index(9011, commands::kPathfinderGuiIndex)}},
        {"own unit -> the lead pad: clear, set", "q1_0001", "q1_0000", e.pad(900, 9002), lead_tile,
         {commands::ev::set_gui_index(9001, commands::kNoGuiIndex), commands::ev::set_gui_index(9002, commands::kPathfinderGuiIndex)}},
        {"a unit that may not lead: refused", "q1_0002", "q1_0000", e.pad(900, 9003), lead_tile, {}},
        {"a unit -> an empty lead pad: merge, set", "q1_0001", "q2_0000", e.pad(900, 9002), empty_tile,
         {fleet_management_event(commands::kFleetMerge, 901, 900, 9002), commands::ev::set_gui_index(9002, commands::kPathfinderGuiIndex)}},
        {"a land unit -> the lead pad: refused", "land_fleet_00", "q1_0000", e.pad(0, 8001, true), lead_tile, {}},
    };
    for (const Case& c : cases) {
        const Out co = e.classic_drag(c.classic_from, c.classic_to);
        const Out mo = e.modern_drag(c.modern_from, c.modern_to);
        const bool ok = co.cmds == c.want && co.valid == !c.want.empty() && same(co, mo);
        if (!ok) std::printf("  lead drop: %s\n", c.what);
        CHECK(ok);
    }
    // The lead tile drags its unit like a stack (onto the other fleet: Classic's merge from the header pad).
    const Out co = e.classic_drag("q1_0000", "q2_0001");
    const Out mo = e.modern_drag(lead_tile, e.pad(901, 9011));
    CHECK(co.cmds == std::vector<commands::EventData>{fleet_management_event(commands::kFleetMerge, 901, 900, 9001)} && same(co, mo));
    // An empty lead tile neither drags nor selects.
    const Out click = e.click(e.modern, empty_tile, 50.0);
    CHECK(click.acts.empty() && click.cmds.empty());
}

int main() {
    test_organiser_view();
    test_lead_parity();
    test_drop_parity();
    test_slot_parity();
    test_stale_drag();
    test_foreign();
    test_click_parity();
    test_radar_parity();
    test_drag_state();
    test_map_fleet_onto_rows();
    test_classic_unchanged();
    if (g_fail) {
        std::printf("alamo_ui_hud_modern_organiser_tests: %d failure(s)\n", g_fail);
        return 1;
    }
    std::printf("alamo_ui_hud_modern_organiser_tests: all passed\n");
    return 0;
}
