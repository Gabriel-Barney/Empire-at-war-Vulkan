// Shared P2 test fixture: a trimmed copy of the vanilla CommandBarComponents.xml entries the tactical HUD uses
// (plus a tag-named component and a duplicate), and a synthetic MT_CommandBar atlas (entries + a procedurally
// drawn 256 x 256 RGBA image) so the HUD renders without game data.
#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iterator>
#include <string_view>
#include <vector>

#include "alamo/data/xml.hpp"
#include "alamo/ui/hud_components.hpp"
#include "alamo/ui/hud_draw.hpp"
#include "alamo/ui/tactical_hud.hpp"

namespace hud_test {

// #597: the bounding box of a set of text glyph quads (canvas units, +Y up).
struct TextRect {
    float x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f;
    size_t quads = 0;
    bool empty() const { return quads == 0; }
    float width() const { return empty() ? 0.0f : x1 - x0; }
    float height() const { return empty() ? 0.0f : y1 - y0; }
    bool intersects(const TextRect& o) const { return !empty() && !o.empty() && x0 < o.x1 && o.x0 < x1 && y0 < o.y1 && o.y0 < y1; }
};

// Every text glyph quad of a draw list (Text and ModelText, all layers) as sorted min / max corners.
inline std::vector<std::array<float, 4>> text_boxes(const alamo::ui::HudDrawList& dl) {
    using alamo::ui::HudList;
    std::vector<std::array<float, 4>> out;
    for (uint32_t l = 0; l < alamo::ui::kHudLayers; ++l)
        for (const HudList list : {HudList::Text, HudList::ModelText})
            for (const alamo::ui::HudQuad& q : dl.items(l, list)) out.push_back({q.min.x, q.min.y, q.max.x, q.max.y});
    std::sort(out.begin(), out.end());
    return out;
}

// The box of the glyph quads in `with` that are not in `without` (multiset difference): the text one frame field adds.
inline TextRect added_text(const std::vector<std::array<float, 4>>& with, const std::vector<std::array<float, 4>>& without) {
    std::vector<std::array<float, 4>> d;
    std::set_difference(with.begin(), with.end(), without.begin(), without.end(), std::back_inserter(d));
    TextRect r;
    for (const auto& b : d) {
        r.x0 = std::min(r.x0, b[0]);
        r.y0 = std::min(r.y0, b[1]);
        r.x1 = std::max(r.x1, b[2]);
        r.y1 = std::max(r.y1, b[3]);
        ++r.quads;
    }
    return r;
}

// #597: the planet panel's text blocks in one TacticalHud build of `f` (name, tech, population and the fleet cohesion
// line), each found as the glyphs its field adds; `empty_digest_kept` = clearing the cohesion text gives the same draw
// list as a build that never had one.
struct PlanetPanelTexts {
    TextRect name, tech, pop, cohesion;
    bool empty_digest_kept = false;
};
inline PlanetPanelTexts planet_panel_texts(alamo::ui::TacticalHud& hud, const alamo::ui::TacticalHudFrame& f) {
    PlanetPanelTexts out;
    alamo::ui::TacticalHudFrame g = f;
    auto boxes = [&] {
        hud.build(g, {}, {});
        return text_boxes(hud.draw_list());
    };
    g.cohesion_text.clear();
    const auto plain = boxes();
    const uint64_t plain_digest = hud.draw_list().digest();
    g = f;
    const auto full = boxes();
    out.cohesion = added_text(full, plain);
    g.cohesion_text.clear();
    g.planet_name.clear();  // the cohesion line goes with the name
    out.name = added_text(plain, boxes());
    g = f;
    g.tech = -1;
    out.tech = added_text(full, boxes());
    g = f;
    g.pop = -1;
    out.pop = added_text(full, boxes());
    g = f;
    g.cohesion_text.clear();
    hud.build(g, {}, {});
    out.empty_digest_kept = hud.draw_list().digest() == plain_digest;
    hud.build(f, {}, {});
    return out;
}

inline const char* kComponentsXml = R"(<?xml version="1.0"?>
<CommandBarComponents>
  <CommandBarComponent Name="i_main_skirmish">
    <Model_Name>i_tactical_controls.alo</Model_Name><Type>Shell</Type><Group>Shells</Group>
    <Model_Offset_X>True</Model_Offset_X><Model_Offset_Y>True</Model_Offset_Y><Hidden> True </Hidden>
  </CommandBarComponent>
  <CommandBarComponent Name="st_bracket_small">
    <Type>TextButton</Type>
    <Icon_Alternate_Texture_Name>i_icon_select_small_l.tga i_icon_select_small_r.tga</Icon_Alternate_Texture_Name>
    <No_Shell>True</No_Shell><Color>0 255 0 255</Color><Base_Layer>0</Base_Layer><Group>Select</Group>
  </CommandBarComponent>
  <CommandBarComponent Name="st_bracket_medium">
    <Type>TextButton</Type>
    <Icon_Alternate_Texture_Name>i_icon_select_med_l.tga i_icon_select_med_r.tga</Icon_Alternate_Texture_Name>
    <No_Shell>True</No_Shell><Color>0 255 0 255</Color><Base_Layer>0</Base_Layer>
  </CommandBarComponent>
  <CommandBarComponent Name="st_bracket_large">
    <Type>TextButton</Type>
    <Icon_Alternate_Texture_Name>i_icon_select_big_l.tga i_icon_select_big_r.tga</Icon_Alternate_Texture_Name>
    <No_Shell>True</No_Shell><Color>0 255 0 255</Color><Base_Layer>0</Base_Layer>
  </CommandBarComponent>
  <CommandBarComponent Name="st_health">
    <Type>Bar</Type>
    <Bar_Texture_Name>i_bar_control_wide.tga i_bar_control_wide.tga i_bar_control_wide.tga i_bar_control_wide.tga i_bar_control_wide.tga i_bar_control_wide.tga i_bar_control_wide.tga i_bar_control_wide.tga i_bar_control_wide.tga i_bar_control_wide.tga i_bar_control_wide.tga </Bar_Texture_Name>
    <Bar_Overlay_Name>i_bar_health_small0.tga i_bar_health_small1.tga i_bar_health_small2.tga i_bar_health_small3.tga i_bar_health_small4.tga i_bar_health_small5.tga i_bar_health_small6.tga i_bar_health_small7.tga i_bar_health_small8.tga i_bar_health_small9.tga i_bar_health_small10.tga </Bar_Overlay_Name>
    <Max_Bar_Level>10</Max_Bar_Level><Base_Layer>0</Base_Layer><No_Shell>True</No_Shell><Smooth_Bar>True</Smooth_Bar>
    <Pixel_Align>False</Pixel_Align><Offset>0 0</Offset><Outlined_Bar> true</Outlined_Bar>
  </CommandBarComponent>
  <CommandBarComponent Name="st_shields">
    <Type>Bar</Type>
    <Bar_Texture_Name>i_bar_control_wide.tga</Bar_Texture_Name>
    <Bar_Overlay_Name>i_bar_shield_small10.tga</Bar_Overlay_Name>
    <Max_Bar_Level>10</Max_Bar_Level><Base_Layer>0</Base_Layer><No_Shell>True</No_Shell><Smooth_Bar>True</Smooth_Bar>
  </CommandBarComponent>
  <CommandBarComponent Name="st_control_group">
    <Type>TextButton</Type><No_Shell>True</No_Shell><Text_Color>255 255 255 255</Text_Color><Base_Layer>0</Base_Layer>
    <Scale>1</Scale><Font_Name>EmpireAtWar-Medium</Font_Name><Font_Point_Size>9</Font_Point_Size>
    <Text_Offset>0 -32</Text_Offset><Text_Outline>True</Text_Outline>
  </CommandBarComponent>
  <CommandBarComponent Name="generic_flytext">
    <Type>TextButton</Type><Font_Name>EmpireAtWar-Medium</Font_Name><Font_Point_Size>7</Font_Point_Size>
    <Text_Emboss>True</Text_Emboss><Text_Outline>False</Text_Outline><Text_Color>255 255 0 255</Text_Color>
    <No_Shell>true</No_Shell><Base_Layer>1</Base_Layer>
  </CommandBarComponent>
  <CommandBarComponent Name="radar"><Type>Button</Type><Size>171 175</Size></CommandBarComponent>
  <CommandBarComponent Name="radar_blip"><Type>Button</Type><Icon_Texture_Name>i_radar_focus.tga</Icon_Texture_Name>
    <Base_Layer>5</Base_Layer><No_Shell>True</No_Shell></CommandBarComponent>
  <CommandBarComponent Name="s_select_00">
    <Type>TextButton</Type><Blank_Texture_Name>i_button_pad_rebel.tga i_button_pad_empire.tga</Blank_Texture_Name>
    <Font_Name>EmpireAtWar-Medium</Font_Name><Font_Point_Size>6</Font_Point_Size><Base_Layer>4</Base_Layer>
    <Group>Tactical_Selection</Group><Text_Offset2>0 -16</Text_Offset2><Text_Outline>True</Text_Outline>
    <Tooltip_Text>TEXT_PRODUCTION_QUEUE_FULL TEXT_PRODUCTION_POP_CAP_REACHED</Tooltip_Text>
  </CommandBarComponent>
  <CommandBarComponent Name="s_select_01"><Type>TextButton</Type><Base_Layer>4</Base_Layer>
    <Blank_Texture_Name>i_button_pad_rebel.tga i_button_pad_empire.tga</Blank_Texture_Name>
    <Font_Name>EmpireAtWar-Medium</Font_Name><Font_Point_Size>6</Font_Point_Size><Text_Outline>True</Text_Outline></CommandBarComponent>
  <CommandBarComponent Name="s_health_00"><Type>Bar</Type><Base_Layer>5</Base_Layer><Smooth_Bar>True</Smooth_Bar></CommandBarComponent>
  <CommandBarComponent Name="b_option_t"><Type>TextButton</Type><Base_Layer>3</Base_Layer></CommandBarComponent>
  <CommandBarComponent Name="Text_Credits_tactical">
    <Type>TextButton</Type><Icon_Texture_Name>i_icon_money_big.tga</Icon_Texture_Name>
    <Font_Name>EmpireAtWar-Medium</Font_Name><Font_Point_Size>7</Font_Point_Size><Text_Color>255 212 33 255</Text_Color>
    <Right_Justified>True</Right_Justified><Text_Offset>-10 0</Text_Offset>
  </CommandBarComponent>
  <tooltip_back><Type>TextButton</Type><Base_Layer>10</Base_Layer><No_Shell>True</No_Shell></tooltip_back>
  <CommandBarComponent Name="st_health"><Type>Bar</Type><Max_Bar_Level>10</Max_Bar_Level><Smooth_Bar>True</Smooth_Bar>
    <Bar_Texture_Name>i_bar_control_wide.tga</Bar_Texture_Name>
    <Bar_Overlay_Name>i_bar_health_small0.tga i_bar_health_small10.tga</Bar_Overlay_Name><No_Shell>True</No_Shell></CommandBarComponent>
</CommandBarComponents>
)";

inline alamo::ui::ComponentTable load_table() {
    alamo::ui::ComponentTable t;
    const alamo::data::XmlDocument doc = alamo::data::XmlDocument::parse(std::string_view(kComponentsXml), "test.xml");
    if (doc.ok()) t.parse(doc.root());
    return t;
}

// Synthetic atlas entries (pixel rectangles in a 256 x 256 texture).
inline alamo::ui::TextureAtlas make_atlas() {
    alamo::ui::TextureAtlas a;
    a.texture_width = a.texture_height = 256;
    a.add({"i_icon_select_small_l.tga", 0, 0, 6, 16, true});
    a.add({"i_icon_select_small_r.tga", 8, 0, 6, 16, true});
    a.add({"i_icon_select_big_l.tga", 16, 0, 10, 32, true});
    a.add({"i_icon_select_big_r.tga", 28, 0, 10, 32, true});
    a.add({"i_bar_control_wide.tga", 0, 40, 34, 6, true});
    a.add({"i_bar_health_small10.tga", 0, 48, 34, 6, true});
    a.add({"i_bar_health_small0.tga", 0, 56, 34, 6, true});
    a.add({"i_bar_shield_small10.tga", 0, 64, 34, 6, true});
    a.add({"i_button_pad_rebel.tga", 64, 0, 40, 40, true});
    a.add({"i_button_pad_empire.tga", 104, 0, 40, 40, true});
    a.add({"i_icon_money_big.tga", 144, 0, 14, 14, true});
    a.add({"i_radar_map_grid.tga", 64, 64, 64, 64, true});
    return a;
}

// RGBA8 pixels for make_atlas(): bracket corners (white, '[' / ']' shapes), bar frame (dark, light edge), health
// fill (green), shield fill (blue), pads (grey / steel), money icon (gold disc), radar grid (cyan lines).
inline std::vector<uint8_t> make_atlas_rgba() {
    std::vector<uint8_t> px(256 * 256 * 4, 0);
    auto put = [&](uint32_t x, uint32_t y, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
        uint8_t* p = &px[(static_cast<size_t>(y) * 256 + x) * 4];
        p[0] = r;
        p[1] = g;
        p[2] = b;
        p[3] = a;
    };
    auto bracket = [&](uint32_t x0, uint32_t w, uint32_t h, bool left) {
        for (uint32_t y = 0; y < h; ++y)
            for (uint32_t x = 0; x < w; ++x) {
                const bool vertical = left ? x < 2 : x >= w - 2;
                const bool arm = (y < 2 || y >= h - 2);
                if (vertical || arm) put(x0 + x, y, 255, 255, 255, 255);
            }
    };
    bracket(0, 6, 16, true);
    bracket(8, 6, 16, false);
    bracket(16, 10, 32, true);
    bracket(28, 10, 32, false);
    for (uint32_t y = 0; y < 6; ++y)
        for (uint32_t x = 0; x < 34; ++x) {
            const bool edge = x == 0 || y == 0 || x == 33 || y == 5;
            put(x, 40 + y, edge ? 200 : 20, edge ? 200 : 20, edge ? 200 : 20, 230);
            const auto shade = static_cast<uint8_t>(255 - y * 25);
            if (!edge) {
                put(x, 48 + y, 30, shade, 30, 255);
                put(x, 56 + y, 0, 0, 0, 0);
                put(x, 64 + y, 40, 120, shade, 255);
            }
        }
    for (uint32_t y = 0; y < 40; ++y)
        for (uint32_t x = 0; x < 40; ++x) {
            const bool edge = x < 2 || y < 2 || x >= 38 || y >= 38;
            put(64 + x, y, edge ? 180 : 70, edge ? 150 : 60, edge ? 90 : 50, 230);
            put(104 + x, y, edge ? 170 : 50, edge ? 180 : 60, edge ? 200 : 75, 230);
        }
    for (uint32_t y = 0; y < 14; ++y)
        for (uint32_t x = 0; x < 14; ++x) {
            const float dx = static_cast<float>(x) - 6.5f, dy = static_cast<float>(y) - 6.5f;
            if (dx * dx + dy * dy <= 42.0f) put(144 + x, y, 255, 200, 40, 255);
        }
    for (uint32_t y = 0; y < 64; ++y)
        for (uint32_t x = 0; x < 64; ++x) {
            const bool line = x % 16 == 0 || y % 16 == 0;
            put(64 + x, 64 + y, 40, line ? 160 : 40, line ? 160 : 60, line ? 200 : 120);
        }
    return px;
}

}  // namespace hud_test
