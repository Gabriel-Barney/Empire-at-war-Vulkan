// alamo_ui_upgrade_sidebar_tests (data-free): the tactical skirmish_upgrade sidebar (#478 / #548; upgrade_sidebar.hpp,
// docs/re/TACTICAL_COMMANDBAR_COMPONENTS.md "skirmish_upgrade (0x232)"). Covered: the two lists of 0x317480 (completed
// and in-progress items, chain sharing 0x2deba0, order of first appearance, the sweep), the specials, the top-set count,
// and 0x301190's placement: the top set, the gap, the column wrap and the frame pieces.
#include <cmath>
#include <cstdio>
#include <map>
#include <string>

#include "alamo/test/support.hpp"
#include "alamo/ui/upgrade_sidebar.hpp"

using namespace alamo::ui;
using ::alamo::test::g_fail;

namespace {

using Kind = UpgradeSidebar::Kind;

bool near(float a, float b) { return std::fabs(a - b) <= 1e-3f; }

// Types: 1 -> 2 -> 3 a chain (reactor 1..3), 4 a lone upgrade, 5 a Tray = False corruption upgrade, 6 a research item
// (in progress only, no Previous level), 7 another lone upgrade.
SidebarInput input() {
    SidebarInput in;
    static const std::map<int32_t, SidebarType> types = {
        {1, {true, -1, 2, "i_reactor1.tga"}}, {2, {true, 1, 3, "i_reactor2.tga"}}, {3, {true, 2, -1, "i_reactor3.tga"}},
        {4, {true, -1, -1, "i_shield.tga"}},  {5, {false, -1, -1, "i_corrupt.tga"}}, {6, {true, -1, -1, ""}},
        {7, {true, -1, -1, "i_armor.tga"}},
    };
    in.type = [](int32_t t) {
        const auto it = types.find(t);
        return it == types.end() ? SidebarType{} : it->second;
    };
    return in;
}

void test_lists() {
    UpgradeSidebar s;
    SidebarInput in = input();
    in.completed = {4, 1, 4};  // a repeated type keeps one slot
    s.update(in);
    CHECK(s.list0().size() == 2 && s.list0()[0].type == 4 && s.list0()[1].type == 1 && s.list1().empty());
    CHECK(s.list0()[1].icon == "i_reactor1.tga" && near(s.list0()[1].fill, 1.0f) && s.list0()[1].text.empty());
    // Level 2 building: the chain slot shows its icon, fill and "%d%%" (truncated).
    in.building = {{2, 0.457f}, {6, 0.1f}};
    s.update(in);
    CHECK(s.list0().size() == 2 && s.list0()[1].type == 1 && s.list0()[1].building == 2 && s.list0()[1].icon == "i_reactor2.tga");
    CHECK(near(s.list0()[1].fill, 0.457f) && s.list0()[1].text == U"45%");
    CHECK(s.list1().size() == 1 && s.list1()[0].type == 6 && s.list1()[0].icon == "missing.tga" && s.list1()[0].text == U"10%");
    // Level 2 done: the higher level replaces the lower in place (one slot per chain); level 1's object alive too.
    in.completed = {4, 1, 2};
    in.building.clear();
    s.update(in);
    CHECK(s.list0().size() == 2 && s.list0()[1].type == 2 && s.list0()[1].building == -1 && s.list0()[1].icon == "i_reactor2.tga");
    CHECK(s.list1().empty());  // swept: nothing names it
    // A lower level met after the higher one is not shown again.
    in.completed = {2, 1, 4};
    s.update(in);
    CHECK(s.list0().size() == 2 && s.list0()[0].type == 4 && s.list0()[1].type == 2);  // the order of first appearance
    // Tray = False slots go to the front; new tray slots are appended.
    in.completed = {2, 4, 7, 5};
    s.update(in);
    CHECK(s.list0().size() == 4 && s.list0()[0].type == 5 && s.list0()[1].type == 4 && s.list0()[2].type == 2 && s.list0()[3].type == 7);
    CHECK(s.top_count() == 1);
    // The specials lead list 0 and count in the top set.
    in.weather_icon = "i_encyclopedia_weather_rain.tga";
    in.interdicted = true;
    s.update(in);
    CHECK(s.list0().size() == 6 && s.list0()[0].kind == Kind::Weather && s.list0()[1].kind == Kind::Interdictor &&
          s.list0()[2].type == 5 && s.top_count() == 3);
    in.weather_icon.clear();
    in.interdicted = false;
    s.update(in);
    CHECK(s.list0().size() == 4 && s.list0()[0].type == 5);
}

void test_place() {
    UpgradeSidebar s;
    SidebarInput in = input();
    UpgradeSidebar::Layout l;
    l.anchor_x = 100.0f;
    l.icons_per_column = 2;
    // One lone tray slot: whole (3), after the gap.
    in.completed = {4};
    s.update(in);
    std::vector<UpgradeSidebar::Placed> p = s.place(l);
    CHECK(p.size() == 1 && p[0].segment == 3 && near(p[0].at.x, 81.0f) && near(p[0].at.y, 304.0f));
    // A top-set corruption slot (no frame), then three tray slots and a research item: top / middle / middle / bottom,
    // wrapping after two rows to a new column level with the first top-set slot.
    in.completed = {5, 4, 1, 7};
    in.building = {{6, 0.5f}};
    s.update(in);
    p = s.place(l);
    CHECK(p.size() == 5);
    if (p.size() == 5) {
        CHECK(p[0].slot->type == 5 && p[0].segment == -1 && near(p[0].at.x, 81.0f) && near(p[0].at.y, 309.0f));
        CHECK(p[1].slot->type == 4 && p[1].segment == 0 && near(p[1].at.y, 309.0f - 38.0f - 5.0f));
        CHECK(p[2].slot->type == 1 && p[2].segment == 1 && near(p[2].at.y, 309.0f - 76.0f - 5.0f));
        CHECK(p[3].slot->type == 7 && p[3].segment == 1 && near(p[3].at.x, 131.0f) && near(p[3].at.y, 309.0f));  // wrapped
        CHECK(p[4].slot->type == 6 && p[4].segment == 2 && near(p[4].at.x, 131.0f) && near(p[4].at.y, 271.0f));
    }
    // Only in-progress items: retail gives every one a top piece (one alone: whole).
    in.completed.clear();
    in.building = {{6, 0.5f}};
    s.update(in);
    p = s.place(l);
    CHECK(p.size() == 1 && p[0].segment == 3);
    in.building = {{6, 0.5f}, {4, 0.2f}};
    s.update(in);
    p = s.place(l);
    CHECK(p.size() == 2 && p[0].segment == 0 && p[1].segment == 0);
    // The specials: weather whole; both: weather top, interdictor bottom.
    in.building.clear();
    in.weather_icon = "w.tga";
    s.update(in);
    p = s.place(l);
    CHECK(p.size() == 1 && p[0].slot->kind == Kind::Weather && p[0].segment == 3);
    in.interdicted = true;
    s.update(in);
    p = s.place(l);
    CHECK(p.size() == 2 && p[0].segment == 0 && p[1].slot->kind == Kind::Interdictor && p[1].segment == 2);
}

}  // namespace

int main() {
    test_lists();
    test_place();
    std::printf(g_fail == 0 ? "ui upgrade sidebar tests passed\n" : "%d ui upgrade sidebar check(s) FAILED\n", g_fail);
    return g_fail == 0 ? 0 : 1;
}
