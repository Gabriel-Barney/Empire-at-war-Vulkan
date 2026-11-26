// [port: Gabriel] The player's HUD style (#520 / #584, docs/DECISIONS.md "Modern HUD: exception to the current focus"):
// Classic (the retail HUD, the default) or Modern (modern_tactical_hud.hpp). Presentation only: never read by the
// simulation. Scripted, smoke and benchmark runs always use Classic.
#pragma once
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

}  // namespace alamo::ui
