// the front end's credits roll (MenuAction ShowCredits: IDC_MENU_CREDITS, legacy docs/re/UI.md §3.4 / §3.5).
// Data: Data\Text\CreditsText_<LANGUAGE>.DAT (legacy docs/formats/SPEC.md §7: an unsorted DAT whose record order is the roll
// order; keys HEADER = a heading line, CENTER = a centred name line, value "[TBL]" = an empty line) and the GameConstants
// Credits_* entries: Credits_Font / Credits_Font_Size, Credits_Header_Top_Color / _Bottom_Color and Credits_Top_Color /
// _Bottom_Color (0..1 RGB gradients), Credits_Spacing, Credits_Margin and Credits_Scroll_Rate.
// #426 (docs/re/CREDITS_ROLL.md, roll setup 0x44e5a0, per-frame scroll 0x44e020, end test 0x44d780) [H]:
// - the line pitch is floor(Credits_Spacing x screen height) pixels; record k (blanks included) has its top edge at
//   height + k x pitch at the start, so line 0 starts at the bottom edge;
// - Credits_Scroll_Rate is screen heights per second, negative = up, moved in whole pixels and at most one pixel per
//   frame (credits_scroll_target_px);
// - the text never fades (Credits_Margin fades only the logos, a follow-up);
// - the roll ends when the highest top edge of every record is at or above the top of the screen;
// - headers and names use the same font; a record whose key contains "HEADER" takes the header colours.
// The backdrop is the "Credits" movie (credits_loop.bik), drawn by the host; `black_backdrop` stands in when it does
// not play. Presentation only.
#pragma once
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "alamo/ui/hud_canvas.hpp"
#include "alamo/ui/hud_draw.hpp"

namespace alamo::data {
struct GameConstants;
}

namespace alamo::ui {

class FontCache;

struct CreditsLine {
    enum class Kind : uint8_t { Blank, Header, Name };
    Kind kind = Kind::Blank;
    std::u32string text;
};

// The retail defaults for a missing tag (GameConstants ctor 0x2c91c0) [H].
struct CreditsStyle {
    std::string font = "Arial Unicode MS";
    float font_size = 10.0f;  // read as an integer
    Rgba8 header_top{255, 255, 255, 255}, header_bottom{255, 255, 255, 255};
    Rgba8 top{255, 255, 255, 255}, bottom{255, 255, 255, 255};
    float spacing = 0.01f;       // line pitch, fraction of the screen height
    float margin = 0.0f;         // the logos' fade bands, fraction of the screen height (logos: follow-up)
    float scroll_rate = -0.01f;  // screen heights per second (negative: up)
};
CreditsStyle credits_style(const data::GameConstants& gc);

// The DAT records in file order (legacy docs/formats/SPEC.md §7). False on a malformed file.
bool parse_credits(std::span<const uint8_t> bytes, std::vector<CreditsLine>& out);
// "Data\Text\CreditsText_<LANGUAGE>.DAT".
std::string credits_path(std::string_view language);
// Pixels the roll may have moved `t` seconds after its start on a screen `screen_height` pixels high (0x44e020 [H]):
// floor(-rate x height x t) + 2. The roll moves one pixel per frame while it is behind this target.
int64_t credits_scroll_target_px(float scroll_rate, int32_t screen_height, double t);

class CreditsRoll {
public:
    void start(std::vector<CreditsLine> lines, const CreditsStyle& style, double now, bool black_backdrop = true);
    void stop() { active_ = false; }
    bool active() const { return active_; }
    // One frame: moves the roll by at most one pixel, then draws the visible lines (the top layer, Text), over a black
    // backdrop when start() asked for one. False (and inactive) once the roll has ended.
    bool build(HudDrawList& dl, FontCache& fonts, const HudCanvas& canvas, double now);
    size_t lines() const { return lines_.size(); }
    size_t drawn() const { return drawn_; }        // lines drawn by the last build
    int64_t moved_px() const { return moved_px_; }  // pixels the roll has moved up
    float last_top_px() const { return last_top_px_; }  // the top edge of line 0 in the last build, pixels from the top

private:
    std::vector<CreditsLine> lines_;
    CreditsStyle style_;
    double t0_ = 0.0;
    bool active_ = false;
    bool black_backdrop_ = true;
    int64_t moved_px_ = 0;
    float last_top_px_ = 0.0f;
    size_t drawn_ = 0;
};

}  // namespace alamo::ui
