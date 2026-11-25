// The tactical battle-cam controller (battle_cam.hpp; docs/re/BATTLE_CAM.md).
#include "alamo/game/battle_cam.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <utility>

namespace alamo::game {

using tactical::Vec3;

namespace {
constexpr float kDegToRad = 3.14159265358979f / 180.0f;
constexpr int32_t kViews = 8;                 // 0x2cbd00: +0x354 wraps after 7
constexpr float kViewPitchLimit = 80.0f;      // 0x2cc3e0: pitch clamped to +-80
constexpr float kViewMouseScale = 0.25f;      // 0x2cc3e0: degrees per pixel of the dominant axis
constexpr float kViewSmoothing = 0.9f;        // 0x4a6790: +0x270
constexpr int32_t kClashHoldFrames = 45;      // 0x4a4ac0: the second eye key's frame
constexpr int32_t kClashOrbitFrames = 600;    // 0x4a4ac0: +0x4c of the orbit key

float avg(const Vec3& e) { return (e.x + e.y + e.z) / 3.0f; }
float length(const Vec3& e) { return std::sqrt(tactical::dot(e, e)); }
}  // namespace

float cinematic_view_yaw(int32_t view) {
    const int32_t v = ((view % kViews) + kViews) % kViews;
    return static_cast<float>((360 - 45 * v) % 360);
}

float cinematic_view_pitch(bool space) { return space ? 30.0f : 45.0f; }

float cinematic_view_distance(const Vec3& half_extents, float zoom) {
    const float r = avg(half_extents) * 3.0f;  // +0x1a4
    const float e = length(half_extents);
    return std::clamp(zoom * r, 3.0f * e, 10.0f * e);
}

Vec3 cinematic_view_offset(float yaw_deg, float pitch_deg, float distance) {
    const float y = yaw_deg * kDegToRad, p = pitch_deg * kDegToRad;
    const float h = distance * std::cos(p);
    return {-h * std::sin(y), h * std::cos(y), distance * std::sin(p)};
}

BattleCamShot cinematic_view_shot(uint32_t target, const Vec3& half_extents, float yaw_deg, float pitch_deg, float zoom) {
    BattleCamShot s;
    s.target = target;
    s.smoothing = kViewSmoothing;
    ShotKey eye;
    eye.frame = 0;
    eye.segment = ShotKey::Segment::Linear;
    eye.attached = true;
    eye.where = cinematic_view_offset(yaw_deg, pitch_deg, cinematic_view_distance(half_extents, zoom));
    s.eye.push_back(eye);
    ShotKey look;
    look.segment = ShotKey::Segment::Linear;
    look.attached = true;
    s.look.push_back(look);
    return s;
}

float hero_clash_radius(const Vec3& half_extents, bool space) {
    const float r = avg(half_extents) * 7.5f;
    const float lo = space ? 250.0f : 125.0f, hi = space ? 1000.0f : 500.0f;
    return r < lo ? lo : std::min(r, hi);
}

BattleCamShot hero_clash_shot(uint32_t hero, uint64_t clash, const Vec3& half_extents, bool space, bool reverse) {
    BattleCamShot s;
    s.target = hero;
    s.clash = clash;
    const float R = hero_clash_radius(half_extents, space);
    const Vec3 at{0.0f, R, 0.67f * R};
    ShotKey a;
    a.frame = 0;
    a.segment = ShotKey::Segment::Hold;
    a.attached = true;
    a.where = at;
    s.eye.push_back(a);
    ShotKey b = a;
    b.frame = kClashHoldFrames;
    b.segment = ShotKey::Segment::Orbit;
    b.period = kClashOrbitFrames;
    b.reverse = reverse;
    s.eye.push_back(b);
    ShotKey look;
    look.segment = ShotKey::Segment::Hold;
    look.attached = true;
    s.look.push_back(look);
    return s;
}

void BattleCam::start(State s, BattleCamShot shot) {
    state_ = s;
    shot_ = std::move(shot);
    ++serial_;
    smooth_clock_ = -1;
}

void BattleCam::stop() {
    state_ = State::None;
    view_ = 0;
    held_ = false;
    shot_ = {};
    smooth_clock_ = -1;
    has_cursor_ = false;
}

bool BattleCam::open_view(const BattleCamGates& g, const BattleCamPicks& p) {
    yaw_ = cinematic_view_yaw(view_);
    pitch_ = cinematic_view_pitch(g.space);
    BattleCamShot s;
    if (!p.view || !p.view(0, yaw_, pitch_, s)) return false;
    start(State::View, std::move(s));
    return true;
}

bool BattleCam::key_down(Key k, const BattleCamGates& g, const BattleCamPicks& p) {
    switch (k) {
        case Key::Tether: return false;  // slot 0x4d: Camera Tether (the host's tether key)
        case Key::MovieMode:
            if (state_ == State::Movie) {
                held_ = true;  // +0x317: an expiring shot is kept while the key is held
                return true;
            }
            return false;
        case Key::CinematicView:
            if (!g.in_game_cinematics) return true;  // swallowed
            if (state_ == State::View) {
                view_ = (view_ + 1) % kViews;
                // [port] a view that cannot be framed (the selection emptied) stops the cam; retail leaves the state
                // to be cleared by the next key (+0x34a = 0).
                if (!open_view(g, p)) stop();
                return true;
            }
            view_ = 0;
            open_view(g, p);  // outside state 2: view 0; nothing to frame = nothing changes
            return true;
        case Key::Escape:
        case Key::Control:
        case Key::Other: break;
    }
    if (state_ == State::View && view_ != 0) {  // a view above 0: any key but Ctrl stops the cam
        if (k != Key::Control) stop();
        return true;
    }
    if (state_ != State::None && k == Key::Escape) {
        stop();
        return true;
    }
    return false;
}

bool BattleCam::movie_key_up(const BattleCamGates& g, const BattleCamPicks& p) {
    held_ = false;
    if (!g.combat_cinematic || g.controls_locked) return false;
    if (state_ == State::Movie) {  // the next clash (never the same one); none = the shot runs on
        BattleCamShot s;
        if (g.in_game_cinematics && p.clash && p.clash(shot_.clash, s)) {
            start(State::Movie, std::move(s));
            return true;
        }
        return false;
    }
    if (state_ == State::View) {
        stop();
        return true;
    }
    toggle(g, p);
    return active();
}

void BattleCam::toggle(const BattleCamGates& g, const BattleCamPicks& p) {
    if (active()) {
        stop();
        return;
    }
    stop();
    // [port] the per-frame update needs In_Game_Cinematics (0x2cd870), so without it no shot starts.
    BattleCamShot s;
    if (g.in_game_cinematics && p.clash && p.clash(0, s)) start(State::Movie, std::move(s));
}

void BattleCam::mouse(int32_t x, int32_t y, uint32_t width, uint32_t height, bool button_or_wheel, const BattleCamPicks& p) {
    if (!active()) return;
    const int32_t dx = has_cursor_ ? x - cursor_x_ : 0, dy = has_cursor_ ? y - cursor_y_ : 0;
    cursor_x_ = x;
    cursor_y_ = y;
    has_cursor_ = true;
    if (state_ == State::Movie) {
        const bool small = static_cast<float>(std::abs(dx)) < static_cast<float>(width) / 20.0f &&
                           static_cast<float>(std::abs(dy)) < static_cast<float>(height) / 20.0f;
        if (button_or_wheel || !small) stop();
        return;
    }
    if (dx == 0 && dy == 0) return;  // Cinematic View: buttons and the wheel do not end it
    if (std::abs(dy) > std::abs(dx)) {
        pitch_ = std::clamp(pitch_ + static_cast<float>(dy) * kViewMouseScale, -kViewPitchLimit, kViewPitchLimit);
    } else {
        yaw_ = std::fmod(yaw_ - static_cast<float>(dx) * kViewMouseScale, 360.0f);
        if (yaw_ < 0.0f) yaw_ += 360.0f;
    }
    BattleCamShot s;  // 0x4ac830: the eye key rebuilt; the shot clock and the smoothing run on
    if (p.view && p.view(shot_.target, yaw_, pitch_, s)) shot_.eye = std::move(s.eye);
}

void BattleCam::clash_ended(const BattleCamPicks& p) {
    if (state_ != State::Movie || shot_.clash == 0) return;
    BattleCamShot s;
    if (p.clash && p.clash(shot_.clash, s)) start(State::Movie, std::move(s));
    else stop();
}

Vec3 BattleCam::smooth_eye(const Vec3& keyed, int32_t clock) {
    if (shot_.smoothing <= 0.0f || smooth_clock_ < 0 || clock <= 0) {
        smooth_clock_ = clock;
        smooth_eye_ = keyed;
        return keyed;
    }
    if (clock > smooth_clock_) {  // one step per shot frame since the last one
        const float f = std::pow(shot_.smoothing, static_cast<float>(clock - smooth_clock_));
        smooth_eye_ = smooth_eye_ * f + keyed * (1.0f - f);
        smooth_clock_ = clock;
    }
    return smooth_eye_;
}

}  // namespace alamo::game
