// The tactical upgrade sidebar, component skirmish_upgrade (0x232; #478, #548; docs/re/TACTICAL_COMMANDBAR_COMPONENTS.md
// "skirmish_upgrade (0x232)"). Retail rebuilds two slot lists every second HUD update (0x317480 [H]) and places them every
// frame (0x301190 [H]):
//
//   list 0 (+0x1130)  the specials (the land weather slot, the space interdictor slot), the Show_In_Sidebar_Tray = False
//                     slots (the top set: no frame), then the tray slots: completed upgrades, one slot per upgrade chain
//                     (0x2deba0), with the next level's progress while it builds;
//   list 1 (+0x1148)  in-progress items with no Previous level (the tray's end).
//
// A slot keeps its place in its list across rebuilds (order of first appearance); a slot nothing names any more goes.
// Presentation only: the host supplies what the local player and its allies own and build.
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "alamo/ui/hud_canvas.hpp"

namespace alamo::ui {

// One type as the sidebar reads it (the host's resolver).
struct SidebarType {
    bool tray = true;        // Show_In_Sidebar_Tray (+0xcb; default true)
    int32_t previous = -1;   // Previous_Upgrade_Level_Type (+0xa60), -1 none
    int32_t next = -1;       // Next_Upgrade_Level_Type (+0xa70), -1 none
    std::string icon;        // Icon_Name (+0x1820); "" -> "missing.tga"
};

// What one rebuild reads.
struct SidebarInput {
    // Completed: the live DUMMY_UPGRADE objects of the local player and its allies whose type has
    // Show_In_Sidebar_When_Complete, in object-list order (types may repeat).
    std::vector<int32_t> completed;
    // In progress: the head item of production queues 2 (Tactical_Units) and 3 (Tactical_Upgrades) of the local player
    // and its allies, in player order, when it has DUMMY_UPGRADE and Show_In_Sidebar_While_Building. progress 0..1.
    struct Building {
        int32_t type = -1;
        float progress = 0.0f;
    };
    std::vector<Building> building;
    std::string weather_icon;  // land: the current weather's icon (0x468860; "" none)
    bool interdicted = false;  // non-skirmish space: the local player's retreat is refused by an interdictor (0x2ded90)
    std::function<SidebarType(int32_t type)> type;
};

class UpgradeSidebar {
public:
    enum class Kind : uint8_t { Upgrade, Weather, Interdictor };
    struct Slot {
        Kind kind = Kind::Upgrade;
        int32_t type = -1;      // user[0]: the type shown (Upgrade)
        int32_t building = -1;  // user[2]: the next level in progress on this chain slot, -1 none
        bool tray = true;       // the type's Show_In_Sidebar_Tray
        std::string icon;
        float fill = 1.0f;      // the build fill: 1 complete, else the progress
        std::u32string text;    // "%d%%" while in progress, else ""
        bool stale = false;     // user[1] (internal)
    };
    // 0x317480: one rebuild. Specials first in list 0 [port: retail can paint an older list-0 slot as a special, see the doc].
    void update(const SidebarInput& in);
    void clear();
    const std::vector<Slot>& list0() const { return list0_; }
    const std::vector<Slot>& list1() const { return list1_; }
    // The top set's size T (0x301190): the specials, then the run of list-0 slots whose type has Tray = False.
    size_t top_count() const;

    // 0x301190: where each slot goes, in GUI units (centred, +Y up, as the component's Offset), and the frame piece it
    // takes from Blank_Texture_Name (0 top, 1 middle, 2 bottom, 3 whole; -1: none, a Tray = False slot).
    struct Placed {
        const Slot* slot = nullptr;
        Vec2f at{};
        int32_t segment = -1;
    };
    struct Layout {
        Vec2f offset{-19.0f, 309.0f};        // Offset (+0x2f8)
        Vec2f step{50.0f, -38.0f};           // Default_Offset (+0x300): x = the next column, y = the next row
        float gap = -5.0f;                   // Size.x (+0x2e0): added to y between the top set and the tray
        int32_t icons_per_column = 13;       // GameConstants Icons_Per_Column (0x124c)
        float anchor_x = 0.0f;               // good_hero_frame's x (HUD +0x10e8)
    };
    std::vector<Placed> place(const Layout& l) const;

private:
    Slot* share(int32_t type, bool list1, bool at_front, const SidebarInput& in);  // 0x2deba0
    std::vector<Slot> list0_, list1_;
};

// "%d%%" of a 0..1 progress, truncated (0x3178ea).
std::u32string sidebar_percent(float progress);

}  // namespace alamo::ui
