// alamo_render - the planetary bombardment lighting flash (#314, docs/re/BOMBARD_FLASH.md). Presentation only.
//
// PlanetaryBombardManager 0x52a110 makes one light per bombardment shot when the faction's
// Bombardment_Lighting_Seconds_List has a positive entry for the shot's salvo index i [H]: at the shot's position, with
// Bombardment_Lighting_Color_List[i] and Bombardment_Lighting_Intensity_List[i] (0 past the end of either), living
// FPS x seconds[i] frames (truncated). Every frame (0x52aa50, at the start of the manager update, before that frame's
// new shots) each light, at age a of life L:
//   envelope e = a / (0.85 L) up to 0.85 L, then 1 - (a - 0.85 L) / (0.15 L), clamped to [0, 1]
//   colour   = e x intensity x colour
//   direction from the camera's look-at: d = (pos.xy - look.xy) / 1500; |d| >= 1 -> (d / |d|, 0), else (d, sqrt(1 - |d|^2))
// then ages by one frame and is dropped at age >= L. The lights left are added to the scene's fill spherical harmonics
// as directional lights (0x236550, weight 1), and 0.4 x the sum of their colours to the scene ambient (0x2773e0 ->
// 0x277830, the ambient part only while the render-path global 0x140a12670 is 0 [M]). No DynamicLighting option gates
// it [H]. Ghidra types the light record as ints; it is read here as floats, with the life and age as frame counts [M].
#pragma once
#include <cstdint>
#include <vector>

#include "alamo/render/lighting.hpp"

namespace alamo::render {

class BombardFlashes {
public:
    static constexpr float kRiseShare = 0.85f;      // 0x52aa50: the envelope peaks at 0.85 of the life [H]
    static constexpr float kDirScale = 1.0f / 1500.0f;  // 0.00066666666 [H]
    static constexpr float kAmbientShare = 0.4f;    // [H]

    // One shot's light (0x52a110): `life_frames` = (int)(FPS x seconds); <= 0 makes none.
    void add(assets::Float3 at, assets::Float3 color, float intensity, int32_t life_frames);
    // One game frame (0x52aa50): every light's colour and direction for this frame, then ages and drops.
    void step(float look_x, float look_y);
    // The lights of the last step (colour already scaled, intensity 1) and their ambient share.
    const std::vector<DirLight>& lights() const { return lights_; }
    assets::Float3 ambient() const { return ambient_; }
    size_t live() const { return flashes_.size(); }
    void clear();

private:
    struct Flash {
        assets::Float3 at, color;
        float intensity = 0.0f;
        int32_t life = 0, age = 0;
    };
    std::vector<Flash> flashes_;  // creation order
    std::vector<DirLight> lights_;
    assets::Float3 ambient_{0, 0, 0};
};

}  // namespace alamo::render
