// [port: Gabriel, 2026-11-13] The rename prompt for fleets and ships (docs/DECISIONS.md "Fleet and ship names"; retail has
// no names). A double click on an own galactic organiser pad (HudAction::Rename) opens it with the object's current name;
// while it is open it takes the keyboard: WM_CHAR text is appended (at most commands::kMaxObjectNameChars code points),
// Backspace deletes the last character, Enter submits, Esc cancels. The host turns a submit into
// commands::rename_object_event(object, utf8()) - an empty text clears the name (the default name is shown again).
// Drawn as a small box centred near the top of the screen, on the top HUD layer of the dialogs list. The texts are the
// port's own (English, no MasterTextFile key).
#pragma once
#include <cstdint>
#include <string>
#include <string_view>

#include "alamo/commands/fleet_group.hpp"  // kFleetGroupNameMax
#include "alamo/ui/hud_draw.hpp"

namespace alamo::ui {

class FontCache;
struct HudCanvas;

class RenamePrompt {
public:
    enum class Result : uint8_t { None, Submit, Cancel };

    // `fleet`: the title says fleet (true) or ship (false). `current` = the name now (UTF-8, "" = none).
    void open(uint32_t object, bool fleet, std::string_view current);
    // [port: Gabriel, #176] a tactical fleet group (docs/DECISIONS.md "Fleet groups: exception to the current focus"):
    // object() = the group id; the text stays within commands::kFleetGroupNameMax UTF-8 bytes (whole characters). The
    // host sends a FleetGroupOp::Rename on submit.
    void open_fleet_group(uint16_t group, std::string_view current);
    void close() { active_ = false; }
    bool active() const { return active_; }
    uint32_t object() const { return object_; }
    bool fleet() const { return fleet_ && !fleet_group_; }
    bool fleet_group() const { return fleet_group_; }
    const std::u32string& text() const { return text_; }
    std::string utf8() const;  // text() as UTF-8

    // One frame of keyboard input while open: `typed` = printable WM_CHAR text, then the Backspace / Enter / Esc presses.
    // Submit and Cancel close the prompt. Nothing happens while closed (Result::None).
    Result update(std::u32string_view typed, bool backspace, bool enter, bool escape);

    // The box, title, text with a blinking caret, and the key hint. Nothing while closed.
    void build(HudDrawList& dl, FontCache& fonts, const HudCanvas& canvas, double now) const;

private:
    bool active_ = false, fleet_ = false, fleet_group_ = false;
    bool fits(char32_t c) const;  // one more character keeps the text within this kind's limit
    uint32_t object_ = 0;
    std::u32string text_;
};

}  // namespace alamo::ui
