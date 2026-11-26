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

private:
    void build_status(const GalacticHudFrame& f);
    void build_controls(const GalacticHudFrame& f);
    void build_heroes(const GalacticHudFrame& f, float top);
    void build_deck(const GalacticHudFrame& f);
    float build_planet_card(const GalacticHudFrame& f, const GalacticPlanetInfo& p, Vec2f top_left, float height);
    float build_fleets(const GalacticHudFrame& f, const GalacticPlanetInfo& p, Vec2f top_left, float height, float max_width);
    float build_queue(const GalacticHudFrame& f, Vec2f top_right, float height, float max_width);
    void build_options(const GalacticHudFrame& f, const GalacticPlanetInfo& p, Vec2f top_left, float height, float width);

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
    ModernGalacticStats stats_;
};

}  // namespace alamo::ui
