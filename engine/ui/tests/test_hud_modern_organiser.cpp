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
        build();
        r.world.mouse_x = static_cast<int32_t>(to_px.x);
        r.world.mouse_y = static_cast<int32_t>(to_px.y);
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
        return drag(classic, fc, classic_px(fc), tc, tc >= 0 ? classic_px(tc) : world_px);
    }
    Out modern_drag(int32_t from, int32_t to, Vec2f world_px = {}) {
        CHECK(from >= 0);
        return drag(modern, from, modern_px(from), to, to >= 0 ? modern_px(to) : world_px);
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
            world_per_px = std::max({world_per_px, std::abs(p1.x - p0.x) * 2.0f * fit.half / size.x, std::abs(p1.y - p0.y) * 2.0f * fit.half / size.y});
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

int main() {
    test_organiser_view();
    test_drop_parity();
    test_foreign();
    test_click_parity();
    test_radar_parity();
    test_drag_state();
    test_classic_unchanged();
    if (g_fail) {
        std::printf("alamo_ui_hud_modern_organiser_tests: %d failure(s)\n", g_fail);
        return 1;
    }
    std::printf("alamo_ui_hud_modern_organiser_tests: all passed\n");
    return 0;
}
