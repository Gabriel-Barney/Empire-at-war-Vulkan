// [port: Gabriel] The Modern galactic view (#593, spec #520; docs/DECISIONS.md "Modern HUD: exception to the current
// focus"). A second view over the galactic HUD feed: it takes the GalacticHudFrame the host already builds and sends the
// same commands and HUD actions as the Classic GalacticHud for the same intent, so the simulation, replays and saves
// cannot tell which style the player used.
//
// The host draws the Classic GalacticHud with set_modern_overlay (the map layer: planet labels, fleet buttons and slots,
// ability rows and drags; the objectives, the battle prompt and the screen text) and this view over it. It draws:
//   - the status corner (top left): credits, income per day, population, tech, the planets owned and the day;
//   - the game controls (top right): menu (b_option_g), pause (b_play_pause), fast forward (b_fast_forward) and the
//     holocron (b_story_arc_g, lit while a chapter is unread), whose clicks are the Classic components' clicks, and the
//     HUD style switch (HudAction::HudStyleToggle);
//   - the alert strip (top centre): the newest of GalacticHudFrame::alerts with a count of the others and, for an event
//     with a position, Jump (slot AlertJump) -> HudAction::RadarCameraJump there; not while the battle prompt (the
//     Classic banner at the top centre) is up;
//   - the hero list (left, under the status corner), standing in for the Classic hero bar: its entries
//     (GalacticHudFrame::heroes, sort_hero_entries order, the bar's 11 at most) with where each hero is; its clicks are
//     the bar's (HeroFocusPlanet: the camera on the hero's planet, which it selects; a double click adds
//     HeroZoomPlanet; nothing in hyperspace or while building / returning);
//   - with a planet selected, the deck along the bottom: the planet card (name in the owner's colour, its income, and in
//     the planet close-up Planet Info and Base Layout: the Classic planet_info and advanced_planetary clicks), the fleets
//     in orbit (a click selects an own fleet: the organiser header's GalacticSelect; other players' fleets are shown,
//     inert), the build options under the tabs Space, Ground, Buildings and Heroes (galactic_build_tab) with each
//     option's cost or blocked reason (build_option_text), and the planet's build queue. Build and queue clicks are the
//     Classic pads': left buys (Production at the planet; a greyed option: HudAction::BuildRefused), right cancels, a
//     queue entry's left click cancels it.
//   - #613 the map (the deck's left end, right with the map side option): the Classic galactic radar's blips (a planet's
//     owner colour, the selected one framed) and the camera footprint on galactic_radar_fit's mapping; a left click is the
//     Classic radar's (galactic_radar_click: RadarCameraJump there, never a planet selection). It shows with or without a
//     planet selected.
//   - #613 in the planet close-up (organize_open), the organiser over the deck: a row per own fleet in orbit (the
//     Classic organiser's feed, GalacticProviders::fleets, in the Classic panes' order: galactic_organiser_panes), its
//     header (the whole fleet) and its unit stacks (galactic_unit_stacks: the Classic pads' stacking); with a free orbit
//     slot (an empty Classic pane), the New fleet row, whose slot tiles show while something is dragged; and the
//     land-forces row (GalacticProviders::land_forces, a tile per unit). Clicks are the Classic pads' (GalacticSelect of
//     the pad; a double click on a pad: Rename). A press that moves more than GalacticHud::kDragThresholdPx drags the
//     header (the whole fleet) or the stack's first unit; the release decides by galactic_organiser_drop, as the Classic
//     organiser: another fleet's row, its tiles or its back (merge; a land unit: launch), a New fleet tile (the Classic
//     empty pane: galactic_slot_drop - re-slot, detach or launch into that slot), the land-forces row (land), a planet in
//     the world (move) or empty space (detach a fleet unit). Other players' fleets have no row, so they are never a
//     target; a release on any other HUD control (the Classic map layer's slot ovals included) does nothing. A map fleet
//     dragged from its Classic button onto these rows drops as onto the Classic organiser (organiser_target).
// While the advanced planet map is open this view draws nothing (the planet map is a Classic side panel in both styles).
#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "alamo/commands/command.hpp"
#include "alamo/ui/galactic_hud.hpp"
#include "alamo/ui/hud_input.hpp"
#include "alamo/ui/modern_hud_view.hpp"

namespace alamo::ui {

// The Modern galactic build tabs. A hero type is on Heroes; another structure (ground or orbital) on Buildings; a unit
// on Space or Ground by GalacticBuildOption::space.
enum class GalacticBuildTab : uint8_t { Space, Ground, Buildings, Heroes };
inline constexpr uint32_t kGalacticBuildTabs = 4;
GalacticBuildTab galactic_build_tab(const GalacticBuildOption& o);

// Hit-box ids of the Modern galactic view's own controls (the controls shared with Classic use the Classic ids).
inline constexpr int32_t kModernGalacticFleetBase = kModernHitBase + 0x300;  // + orbit fleet index
inline constexpr int32_t kModernGalacticBuildBase = kModernHitBase + 0x400;  // + build option index (the tab's list)
inline constexpr int32_t kModernGalacticQueueBase = kModernHitBase + 0x500;  // + queue entry index
inline constexpr int32_t kModernGalacticHeroBase = kModernHitBase + 0x600;   // + hero row
inline constexpr int32_t kModernGalacticTabBase = kModernHitBase + 0x700;    // + GalacticBuildTab
inline constexpr int32_t kModernGalacticOrganiserBase = kModernHitBase + 0x900;  // #613 + organiser pad index (organiser_pads)
inline constexpr int32_t kModernGalacticMapHit = kModernHitBase + 9;        // #613 the map, when the table has no radar_galactic
inline constexpr int32_t kModernGalacticLandRowHit = kModernHitBase + 10;   // #613 the land-forces row (a drop target)
inline constexpr int32_t kModernGalacticRowBase = kModernHitBase + 0xA00;   // #613 + organiser fleet row: its back (merge)
inline constexpr int32_t kModernGalacticSlotBase = kModernHitBase + 0xB00;  // #613 + orbit slot 0..2: a New fleet tile

// #613 one organiser tile: a fleet's header (unit 0) or unit stack, or a land unit.
struct ModernOrganiserPad {
    int32_t id = -1;                // its hit box
    bool land = false;              // a land-forces unit
    uint32_t fleet = 0;             // its fleet (0: a land unit)
    uint32_t unit = 0;              // the stack's first unit (a drag takes it); 0: the fleet header
    std::vector<uint32_t> select;   // what a click selects (GalacticSelect ids)
    std::string icon;
    std::u32string name;            // the fleet's or the unit's player-given name ("" none)
    uint32_t count = 1;
};

struct ModernGalacticStats {
    bool status_corner = false;
    // the status corner's values ("" = that row not drawn)
    std::u32string credits_text, income_text, pop_text, tech_text, planets_text, day_text;
    uint32_t controls = 0;  // game control buttons drawn (menu, pause, fast forward, holocron)
    bool style_switch = false;
    std::u32string alert_text;  // the alert strip's event ("" = no strip)
    uint32_t alerts_more = 0;
    bool jump = false;
    uint32_t heroes = 0;  // hero rows drawn
    bool deck = false, planet_card = false;
    std::u32string planet_name;
    uint32_t planet_buttons = 0;  // Planet Info, Base Layout
    uint32_t fleets = 0, enemy_fleets = 0;  // orbit fleet tiles drawn / of them other players'
    uint32_t tabs_live = 0;                 // build tabs with options (of kGalacticBuildTabs)
    GalacticBuildTab tab = GalacticBuildTab::Space;
    uint32_t build_options = 0, build_blocked = 0, queue_entries = 0;
    std::vector<std::u32string> build_texts;  // each drawn option's line: its cost, or its blocked reason
    // #613 the map: drawn, its blips, the camera footprint drawn; the organiser: shown, its fleet rows, unit stacks and
    // land tiles; whether the drag's target under the cursor takes the drop (the target's highlight).
    bool map = false, map_view = false;
    uint32_t map_blips = 0;
    bool organiser = false;
    uint32_t organiser_fleets = 0, organiser_stacks = 0, land_pads = 0;
    uint32_t slot_tiles = 0;       // New fleet tiles drawn (free orbit slots, while something is dragged)
    uint32_t drop_highlights = 0;  // organiser targets drawn lit (under the cursor, taking the drop: this view's or Classic's drag)
    bool drop_valid = false;
    uint32_t key_badges = 0, hit_boxes = 0, quads = 0;
};

class ModernGalacticHud : public ModernHudView {
public:
    // `table` (the CommandBarComponents the Classic HUD uses) must outlive the HUD; it names the shared controls' ids.
    void init(const ComponentTable& table, const TextureAtlas& atlas, FontCache& fonts, const TextDatabase* text = nullptr);
    // The GalacticHud::build interface.
    void build(const GalacticHudFrame& frame);
    const ModernGalacticStats& stats() const { return stats_; }
    // The selected planet (from the frame's planets) of the last build, -1 if none (GalacticHud::selected_planet).
    int32_t selected_planet() const { return selected_; }
    // The build tab shown; kept across planets and builds. A tab without options can be chosen (it lists nothing).
    GalacticBuildTab build_tab() const { return tab_; }
    void set_build_tab(GalacticBuildTab t) { tab_ = t; }
    // One frame of HUD input on the hit boxes of the previous build(): the commands and actions the Classic GalacticHud
    // sends for the same control (a hero row: the hero bar's), plus HudStyleToggle for the style switch and RadarCameraJump
    // for Jump.
    // `now` (seconds) times the hero rows' double click, as the hero bar's.
    void update_input(const RoutedInput& routed, bool shift, bool ctrl, double now, std::vector<commands::EventData>& out,
                      std::vector<HudAction>* actions = nullptr);
    // #613 the organiser drag (GalacticHud::drag's fields: fleet_index 0 a fleet's pad, -1 a land unit; over_planet = the
    // frame's planet under the cursor; over_fleet / valid = the target takes the drop). The host reads it for the drag
    // cursor, sounds and route preview while it is active.
    const GalacticDrag& drag() const { return drag_; }
    // #613 ends the organiser press / drag without a drop. The host calls it whenever this view does not take the input
    // (Classic style, the command bar story-hidden, another screen), so a drag never outlives the view; update_input also
    // ends one the HUD no longer holds the button for.
    void cancel_drag() {
        drag_ = {};
        press_pad_ = -1;
    }
    // #613 the Classic HUD's drag this frame (GalacticHud::drag: a map fleet). While it is active the New fleet tiles show
    // and the organiser target under the cursor is lit when Classic's drop there is valid. Set it before build().
    void set_classic_drag(const GalacticDrag& d) { classic_drag_ = d; }
    // #613 an organiser row's drop target under HUD component `component` of the last build (a fleet's tile, a land tile or
    // the land-forces row); false for any other. The host gives it to the Classic HUD (GalacticHud::set_drop_targets), so a
    // map fleet dragged from its Classic button onto these rows merges or lands as onto the Classic organiser's pads.
    bool organiser_target(int32_t component, GalacticDropTarget& out) const;
    // #613 the organiser tiles of the last build (id = kModernGalacticOrganiserBase + index).
    const std::vector<ModernOrganiserPad>& organiser_pads() const { return org_; }
    // #613 the map's hit box id: the Classic radar_galactic's (its click is that component's), else kModernGalacticMapHit.
    int32_t map_id() const { return radar_ >= 0 ? radar_ : kModernGalacticMapHit; }

private:
    void build_status(const GalacticHudFrame& f);
    void build_controls(const GalacticHudFrame& f);
    void build_heroes(const GalacticHudFrame& f, float top);
    void build_deck(const GalacticHudFrame& f);
    float build_planet_card(const GalacticHudFrame& f, const GalacticPlanetInfo& p, Vec2f top_left, float height);
    float build_fleets(const GalacticHudFrame& f, const GalacticPlanetInfo& p, Vec2f top_left, float height, float max_width);
    float build_queue(const GalacticHudFrame& f, Vec2f top_right, float height, float max_width);
    void build_options(const GalacticHudFrame& f, const GalacticPlanetInfo& p, Vec2f top_left, float height, float width);
    void build_map(const GalacticHudFrame& f);
    void build_organiser(const GalacticHudFrame& f, const GalacticPlanetInfo& p, Vec2f bottom_left, float max_width);
    void build_drag();
    // #613 the organiser drag: what a release over `component` (-1: no HUD control) at pixel `px` drops onto.
    GalacticDropTarget drop_target(int32_t component, Vec2f px) const;
    bool drop(std::vector<commands::EventData>* out) const;
    const ModernOrganiserPad* org_pad(int32_t id) const;
    bool dragging() const { return drag_.active || classic_drag_.active; }
    // A target tile / row `id` is under the cursor and the drag in progress (this view's, else Classic's) takes the drop.
    bool highlight(int32_t id) const {
        return hover_ == id && (drag_.active ? drag_.valid : classic_drag_.active && classic_drag_.valid);
    }

    int32_t option_ = -1, pause_ = -1, fast_forward_ = -1, holocron_ = -1;  // b_option_g, b_play_pause, b_fast_forward, b_story_arc_g
    int32_t planet_info_ = -1, base_layout_ = -1;                           // planet_info, advanced_planetary
    Rgba8 credits_color_{255, 212, 33, 255};  // Text_Credits' Text_Color when the data has one
    float status_bottom_ = 0.0f;              // canvas y under the status corner (the hero list hangs from it)
    bool paused_ = false;
    int32_t selected_ = -1;
    uint32_t selected_id_ = 0;
    GalacticBuildTab tab_ = GalacticBuildTab::Space;
    // this build's clickable slots, by hit index
    std::vector<GalacticFleet> fleets_;
    std::vector<GalacticBuildOption> options_shown_;
    std::vector<HudQueueEntry> queue_;
    std::vector<HeroEntry> heroes_;  // the rows drawn (sort_hero_entries order)
    int32_t last_hero_click_ = -1;   // the hero row of the last left click, its time and pixel (the bar's double click)
    double last_hero_time_ = -1e30;
    int32_t last_hero_x_ = 0, last_hero_y_ = 0;
    bool tab_live_[kGalacticBuildTabs] = {};
    // #613 the map (the Classic radar's component, its rectangle and fit of the last build) and the organiser.
    int32_t radar_ = -1;
    Vec2f map_at_{}, map_size_{};
    GalacticRadarFit map_fit_;
    float map_side_ = 0.0f;  // the map panel's side this build (0: not drawn)
    std::vector<ModernOrganiserPad> org_;
    bool land_row_ = false;  // the land-forces row was drawn (its back is a drop target)
    std::vector<uint32_t> rows_;  // the fleet of each organiser row (its back: kModernGalacticRowBase + row)
    std::vector<int32_t> slots_;  // the free orbit slots with a New fleet tile this build
    GalacticDrag classic_drag_;   // set_classic_drag
    std::vector<GalacticPlanetScreen> planet_px_;
    GalacticDrag drag_;
    int32_t press_pad_ = -1;  // the organiser tile of the left press (a drag source), -1 none
    Vec2f press_px_{};
    int32_t last_pad_click_ = -1;  // the organiser tile of the last plain left click, its time and pixel (double click)
    double last_pad_time_ = -1e30;
    int32_t last_pad_x_ = 0, last_pad_y_ = 0;
    ModernGalacticStats stats_;
};

}  // namespace alamo::ui
