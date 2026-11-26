// Drawing helpers and layout constants shared by the Modern HUD views (modern_hud_view.hpp); internal to alamo_ui.
#pragma once
#include <cstdint>
#include <string>

#include "alamo/ui/hud_draw.hpp"

namespace alamo::ui::modern_draw {

inline constexpr uint32_t kPanelLayer = 3, kControlLayer = 4, kBannerLayer = 11;  // the banner over the panels, as Classic's
inline constexpr float kMargin = 8.0f;  // canvas units from the screen edge
inline constexpr float kPad = 8.0f;     // inside a panel

void add_rect(HudDrawList& dl, uint32_t layer, float x0, float y0, float x1, float y1, Rgba8 color);
// A rectangle outline of dashes (a disabled control, readable without colour).
void dashed_rect(HudDrawList& dl, uint32_t layer, Vec2f c, Vec2f size, Rgba8 color);
std::u32string count_text(uint32_t n);

}  // namespace alamo::ui::modern_draw
