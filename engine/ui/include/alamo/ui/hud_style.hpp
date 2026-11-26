// [port: Gabriel] The player's HUD style (#520 / #584, docs/DECISIONS.md "Modern HUD: exception to the current focus"):
// Classic (the retail HUD, the default) or Modern (modern_tactical_hud.hpp), and the Modern style's options (#590).
// Presentation only: never read by the simulation. Scripted, smoke and benchmark runs always use Classic.
#pragma once
#include <algorithm>
#include <cstdint>
#include <optional>
#include <string_view>

namespace alamo::ui {

enum class HudStyle : uint8_t { Classic, Modern };
// Profile names: "classic", "modern".
std::string_view hud_style_name(HudStyle s);
// A profile value (case-insensitive); nullopt for anything else.
std::optional<HudStyle> parse_hud_style(std::string_view s);
inline HudStyle other_hud_style(HudStyle s) { return s == HudStyle::Modern ? HudStyle::Classic : HudStyle::Modern; }

// The Modern colours: the local faction's accent, a colour-blind-safe palette (Okabe-Ito), or high contrast.
enum class HudColours : uint8_t { Faction, ColourBlind, HighContrast };
// Profile names: "faction", "colourblind", "contrast".
std::string_view hud_colours_name(HudColours c);
std::optional<HudColours> parse_hud_colours(std::string_view s);

// Modern text size choices in percent (the Interface page's list; 115 = 18 px body text at 1080p).
inline constexpr int32_t kModernTextScaleChoices[] = {100, 115, 125, 150, 175, 200};
inline constexpr int32_t kModernTextScaleMinPct = 100, kModernTextScaleMaxPct = 200, kModernTextScaleDefaultPct = 115;
inline constexpr int32_t kModernOpacityMin = 40, kModernOpacityMax = 100;  // panel opacity, percent

// The Modern options (#590; video profile keys hud_text_scale, hud_colours, hud_key_hints, hud_order_labels, hud_map_side,
// hud_panel_opacity, hud_contextual). Greyed on the Interface page while Classic is chosen.
struct ModernHudOptions {
    int32_t text_scale = kModernTextScaleDefaultPct;  // percent, kModernTextScaleMinPct .. kModernTextScaleMaxPct
    HudColours colours = HudColours::Faction;
    bool key_hints = true;      // hotkey badges on the buttons
    bool order_labels = true;   // the order buttons' names (off: the key alone)
    bool map_right = false;     // the deck's map on the right (default left)
    int32_t panel_opacity = 80;  // percent, kModernOpacityMin .. kModernOpacityMax
    bool contextual = true;     // the deck follows the selection (off: always full width)
    friend bool operator==(const ModernHudOptions&, const ModernHudOptions&) = default;
};
inline ModernHudOptions clamp_options(ModernHudOptions o) {
    o.text_scale = std::clamp(o.text_scale, kModernTextScaleMinPct, kModernTextScaleMaxPct);
    o.panel_opacity = std::clamp(o.panel_opacity, kModernOpacityMin, kModernOpacityMax);
    return o;
}

}  // namespace alamo::ui
