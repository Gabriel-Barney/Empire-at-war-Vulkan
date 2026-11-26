// alamo_render - the planetary bombardment lighting flash (bombard_flash.hpp, docs/re/BOMBARD_FLASH.md).
#include "alamo/render/bombard_flash.hpp"

#include <cmath>

namespace alamo::render {

void BombardFlashes::add(assets::Float3 at, assets::Float3 color, float intensity, int32_t life_frames) {
    if (life_frames <= 0) return;
    Flash f;
    f.at = at;
    f.color = color;
    f.intensity = intensity;
    f.life = life_frames;
    flashes_.push_back(f);
}

void BombardFlashes::step(float look_x, float look_y) {
    lights_.clear();
    ambient_ = {0, 0, 0};
    size_t keep = 0;
    for (size_t i = 0; i < flashes_.size(); ++i) {
        Flash& f = flashes_[i];
        const float life = static_cast<float>(f.life), age = static_cast<float>(f.age);
        const float rise = life * kRiseShare;
        float e = rise < age ? 1.0f - (age - rise) / (life - rise) : age / rise;
        e = e < 0.0f ? 0.0f : (e > 1.0f ? 1.0f : e);
        const float s = e * f.intensity;
        DirLight l;
        l.color = {s * f.color.x, s * f.color.y, s * f.color.z};
        l.specular = {0, 0, 0};
        l.intensity = 1.0f;
        float dx = (f.at.x - look_x) * kDirScale, dy = (f.at.y - look_y) * kDirScale;
        const float r = std::sqrt(dx * dx + dy * dy);
        float dz = 0.0f;
        if (r >= 1.0f) {
            dx /= r;
            dy /= r;
        } else {
            dz = std::sqrt(1.0f - r * r);
        }
        l.direction = {dx, dy, dz};
        ++f.age;
        if (f.age >= f.life) continue;  // dropped before the lights are summed
        lights_.push_back(l);
        ambient_.x += l.color.x * kAmbientShare;
        ambient_.y += l.color.y * kAmbientShare;
        ambient_.z += l.color.z * kAmbientShare;
        flashes_[keep++] = f;
    }
    flashes_.resize(keep);
}

void BombardFlashes::clear() {
    flashes_.clear();
    lights_.clear();
    ambient_ = {0, 0, 0};
}

}  // namespace alamo::render
