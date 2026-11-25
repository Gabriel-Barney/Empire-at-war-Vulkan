// The tactical battle-cam controller (battle_cam.hpp; docs/re/BATTLE_CAM.md).
#include "alamo/game/battle_cam.hpp"

#include "alamo/core/ascii.hpp"

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
constexpr float kViewZoomStep = 25.0f;        // 0x4ac520: world units per wheel notch
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
    s.handheld = true;
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

Vec3 terrain_clamped_eye(const Vec3& eye, const std::function<float(float x, float y)>& ground) {
    constexpr float kStep = 15.0f, kOver = 15.0f;  // 0x49cd00: the grid step and the height over the ground
    if (!ground) return eye;
    float h = ground(eye.x, eye.y);
    for (int i = -1; i <= 1; ++i)
        for (int j = -1; j <= 1; ++j) h = std::max(h, ground(eye.x + kStep * static_cast<float>(i), eye.y + kStep * static_cast<float>(j)));
    return {eye.x, eye.y, std::max(eye.z, h + kOver)};
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
    s.handheld = true;
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
    if (shot_.smoothing) smoothing_ = *shot_.smoothing;
    if (shot_.interesting) {
        on_interesting_ = true;
        terrain_clamp_ = true;
    }  // 0x4a53f0 step 2: +0x315 = 1 for every generated shot (sticky)
    last_clock_ = 0;
    ++serial_;
    smooth_clock_ = -1;
}

void BattleCam::stop() {
    state_ = State::None;
    smoothing_ = 0.0f;  // End_Cinematic_Mode
    on_interesting_ = false;
    view_ = 0;
    held_ = false;
    shot_ = {};
    smooth_clock_ = -1;
    has_cursor_ = false;
}

bool BattleCam::open_view(const BattleCamGates& g, const BattleCamPicks& p) {
    yaw_ = cinematic_view_yaw(view_);
    pitch_ = cinematic_view_pitch(g.space);
    uint32_t id = 0;
    if (!p.view || !p.view(0, id, view_extents_)) return false;
    start(State::View, cinematic_view_shot(id, view_extents_, yaw_, pitch_, zoom_));
    terrain_clamp_ = true;  // +0x315 = 1 (sticky)
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
    if (state_ == State::Movie)  // 0x2cda20: the next clash (never the same one), then another interesting object;
        return g.in_game_cinematics && chain(p);  // none = the shot runs on
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
    if (!g.in_game_cinematics) return;
    if (p.clash && p.clash(0, s)) start(State::Movie, std::move(s));
    else if (p.interesting && p.interesting(true, s)) start(State::Movie, std::move(s));  // #568 0x2cd930(ctrl, 1)
}

void BattleCam::mouse(int32_t x, int32_t y, uint32_t width, uint32_t height, bool button, float wheel, const BattleCamPicks& p) {
    const bool button_or_wheel = button || wheel != 0.0f;
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
    // Cinematic View: buttons do nothing; the wheel zooms (0x4ac520) from 3 avg(E) x zoom, the shown distance unclamped.
    // [port] With the object gone the factor is kept (retail clamps to [0, d]); the next step stops the cam anyway.
    uint32_t id = 0;
    if (wheel != 0.0f && p.view && p.view(shot_.target, id, view_extents_)) {
        const float base = 3.0f * avg(view_extents_), e = length(view_extents_);
        if (base > 0.0f) {
            zoom_ = std::clamp(base * zoom_ - kViewZoomStep * wheel, 3.0f * e, 10.0f * e) / base;
            shot_.eye = cinematic_view_shot(id, view_extents_, yaw_, pitch_, zoom_).eye;
        }
    }
    if (dx == 0 && dy == 0) return;
    if (std::abs(dy) > std::abs(dx)) {
        pitch_ = std::clamp(pitch_ + static_cast<float>(dy) * kViewMouseScale, -kViewPitchLimit, kViewPitchLimit);
    } else {
        yaw_ = std::fmod(yaw_ - static_cast<float>(dx) * kViewMouseScale, 360.0f);
        if (yaw_ < 0.0f) yaw_ += 360.0f;
    }
    (void)rebuild_view_eye(p);
}

bool BattleCam::rebuild_view_eye(const BattleCamPicks& p) {
    uint32_t id = 0;  // 0x4ac830: the eye key rebuilt; the shot clock and the smoothing run on
    if (!p.view || !p.view(shot_.target, id, view_extents_)) return false;
    shot_.eye = cinematic_view_shot(id, view_extents_, yaw_, pitch_, zoom_).eye;
    return true;
}

void BattleCam::clash_ended(const BattleCamPicks& p) {
    if (state_ != State::Movie || shot_.clash == 0) return;
    if (!chain(p)) stop();
}

bool BattleCam::chain(const BattleCamPicks& p) {
    BattleCamShot s;
    if (p.clash && p.clash(shot_.clash, s)) {
        start(State::Movie, std::move(s));
        return true;
    }
    if (on_interesting_ && p.interesting && p.interesting(false, s)) {  // only once on an interesting object (+0x360)
        start(State::Movie, std::move(s));
        return true;
    }
    return false;
}

int32_t BattleCam::tick(int32_t clock, bool tab_held, const BattleCamPicks& p) {
    if (!active() || shot_.length <= 0 || clock < shot_.length) return last_clock_ = clock;
    if (state_ != State::Movie) {
        stop();
        return 0;
    }
    if (held_ || tab_held) return last_clock_;  // frozen on the last frame
    if (!chain(p)) stop();
    return 0;
}

Vec3 BattleCam::smooth_eye(const Vec3& keyed, int32_t clock) {
    if (smoothing_ <= 0.0f || smooth_clock_ < 0 || clock <= 0) {
        smooth_clock_ = clock;
        smooth_eye_ = keyed;
        return keyed;
    }
    if (clock > smooth_clock_) {  // one step per shot frame since the last one
        const float f = std::pow(smoothing_, static_cast<float>(clock - smooth_clock_));
        smooth_eye_ = smooth_eye_ * f + keyed * (1.0f - f);
        smooth_clock_ = clock;
    }
    return smooth_eye_;
}


// ---- #568 the interesting-object shot --------------------------------------------------------------------------------
std::vector<size_t> saliency_rank(std::span<const SaliencyFeatures> features, const SaliencyWeights& w) {
    constexpr size_t kCols = 7;
    const size_t n = features.size();
    std::vector<size_t> order(n);
    for (size_t i = 0; i < n; ++i) order[i] = i;
    if (n == 0) return order;
    const auto get = [](const SaliencyFeatures& f, size_t k) {
        const float v[kCols] = {f.size, f.power, f.x, f.y, f.health, f.targets, f.speed};
        return v[k];
    };
    std::vector<float> col(n * kCols);
    for (size_t k = 0; k < kCols; ++k) {
        float lo = get(features[0], k), hi = lo, sum = 0.0f;
        for (const SaliencyFeatures& f : features) {
            lo = std::min(lo, get(f, k));
            hi = std::max(hi, get(f, k));
            sum += get(f, k);
        }
        const float range = hi > lo ? hi - lo : 1.0f;  // a zero range divides by 1
        const float mean = sum / static_cast<float>(n);
        for (size_t i = 0; i < n; ++i) {
            const float v = get(features[i], k);
            float x = (v - lo) / range;
            if (k == 2 || k == 3) x = 1.0f - std::fabs(v - mean) / range;  // x, y: near the centre of the action
            else if (k == 4) x = 1.0f - x;                                  // health: lower is more salient
            col[i * kCols + k] = x;
        }
    }
    const float weight[kCols] = {w.size, w.power, w.x, w.y, w.health, w.targets, w.speed};
    std::vector<float> score(n, 0.0f);
    for (size_t k = 0; k < kCols; ++k) {
        float lo = col[k], hi = lo;
        for (size_t i = 0; i < n; ++i) {
            lo = std::min(lo, col[i * kCols + k]);
            hi = std::max(hi, col[i * kCols + k]);
        }
        const float spread = hi - lo;  // pass 3: a column whose values are all alike counts for nothing
        for (size_t i = 0; i < n; ++i) score[i] += weight[k] * col[i * kCols + k] * spread;
    }
    std::stable_sort(order.begin(), order.end(), [&score](size_t a, size_t b) { return score[a] > score[b]; });
    return order;
}

bool interesting_pick(InterestingState& s, bool first, const InterestingWorld& w, UnsyncedRandom& rng) {
    if (first && !w.selection.empty()) {  // 0x2cd930: a random selected object, no filter; +0x368 untouched
        const uint32_t id = w.selection[static_cast<size_t>(rng.range(0, static_cast<int32_t>(w.selection.size()) - 1))];
        s.current = id;
        s.type = w.type_of ? w.type_of(id) : -1;
        return true;
    }
    // Step A: validate the current object through the stored id (only this step writes it).
    if (s.current == 0) {
        s.stored = 0;
    } else {
        if (s.stored == 0) s.stored = s.current;
        if (s.stored != s.current || !w.type_of || w.type_of(s.stored) < 0) {
            s.current = 0;
            s.stored = 0;
        }
    }
    // Step B: the current object's target, chance 2/3.
    if (s.current != 0 && (!w.may_follow || w.may_follow(s.current)) && rng.range(0, 2) > 0)
        if (const uint32_t t = w.target ? w.target(s.current) : 0; t != 0) {
            s.current = t;
            s.type = w.type_of ? w.type_of(t) : -1;
            return true;
        }
    // Steps C - E: the ranked candidates.
    const size_t n = w.candidate_ids.size();
    if (n == 0 || w.candidate_types.size() != n || w.candidate_features.size() != n) return false;
    const std::vector<size_t> ranked = saliency_rank(w.candidate_features, w.weights);
    const int32_t k = static_cast<int32_t>(std::min<size_t>(n - 1, 10));
    size_t pick = ranked[0];
    for (int32_t i = 0; i < k; ++i) {
        const size_t c = ranked[static_cast<size_t>(rng.range(0, k))];
        if (w.candidate_ids[c] != s.current && w.candidate_types[c] != s.type) {
            pick = c;
            break;
        }
    }
    s.current = w.candidate_ids[pick];
    s.type = w.candidate_types[pick];
    (void)rng.range(0, 3);  // the hardpoint draw; [port] no hardpoint is picked (#571)
    return true;
}

namespace {
float smoothstep01(float t) { return t * t * (3.0f - 2.0f * t); }
float size_fraction(const Vec3& e) { return smoothstep01(std::clamp((length(e) - 30.0f) / 470.0f, 0.0f, 1.0f)); }
ShotKey key(int32_t frame, ShotKey::Segment seg, bool attached, Vec3 where, float ease_out = 0.0f, float ease_in = 0.0f) {
    ShotKey k;
    k.frame = frame;
    k.segment = seg;
    k.attached = attached;
    k.where = where;
    k.ease_out = ease_out;
    k.ease_in = ease_in;
    return k;
}
}  // namespace

BattleCamShot static_shot(const InterestingObject& o, bool space, UnsyncedRandom& rng) {
    BattleCamShot s;
    s.target = o.id;
    s.interesting = true;
    const float psi = rng.rangef(0.0f, 360.0f) * kDegToRad;
    float phi = rng.rangef(20.0f, 45.0f);
    if (space && rng.range(0, 1) != 0) phi = -phi;
    phi *= kDegToRad;
    const float e = length(o.half_extents);
    const float d = rng.rangef(2.0f * e, 4.0f * e);
    const Vec3 c = o.frame.position;  // the ObjBox centre [M: the object's position]
    s.look.push_back(key(0, ShotKey::Segment::Hold, false, c));
    s.eye.push_back(key(0, ShotKey::Segment::Hold, false,
                        c + Vec3{d * std::cos(phi) * std::cos(psi), d * std::cos(phi) * std::sin(psi), d * std::sin(phi)}));
    s.length = 60;
    s.handheld = true;
    s.handheld_frames = 60;
    return s;
}

BattleCamShot orbit_shot(const InterestingObject& o, UnsyncedRandom& rng) {
    BattleCamShot s;
    s.target = o.id;
    s.interesting = true;
    const float t = size_fraction(o.half_extents);
    float r = 2.0f * length(o.half_extents);
    if (rng.range(0, 1) == 1) r = -r;
    ShotKey eye = key(0, ShotKey::Segment::Orbit, true, {r, 0.0f, 1.25f * o.half_extents.z}, 0.25f);
    eye.translate_only = true;
    eye.period = 600;
    eye.reverse = rng.range(0, 1) == 1;
    s.eye.push_back(eye);
    ShotKey look = key(0, ShotKey::Segment::Hold, true, {});
    look.translate_only = true;
    s.look.push_back(look);
    s.length = 90 + static_cast<int32_t>(180.0f * t);
    s.smoothing = 0.9f;
    return s;
}

bool fly_past_shot(const InterestingObject& o, bool space, UnsyncedRandom& rng, BattleCamShot& out) {
    if (!o.model) return false;
    const Vec3 e = o.half_extents;
    const float t = size_fraction(e);
    const float sgn = rng.range(0, 1) != 0 ? -1.0f : 1.0f;
    BattleCamShot s;
    s.target = o.id;
    s.interesting = true;
    s.smoothing = 0.9f;
    // 0x4a1c90 [H]: retail's _stricmp on the type name (docs/re/BATTLE_CAM.md "The three ported templates").
    const bool frigate = ascii_iequals(o.type_name, "Nebulon_B_Frigate") || ascii_iequals(o.type_name, "Alliance_Assault_Frigate");
    if (rng.range(0, 1) == 0 && space && !frigate) {  // A: under the hull, along +Y
        const int32_t T = static_cast<int32_t>((t + 1.0f) * 8.0f * static_cast<float>(kCinematicFps));
        s.eye.push_back(key(0, ShotKey::Segment::Spline, true, {0.0f, -0.6f * e.y, -e.z}, 0.1f));
        s.eye.push_back(key(T, ShotKey::Segment::Spline, true, {0.0f, 3.0f * e.y, -1.5f * e.z}));
        s.look.push_back(key(0, ShotKey::Segment::Hold, true, {0.0f, -6.0f * e.y, 0.0f}));
        s.length = T;
    } else {  // B: across the top front, along the start's axes
        const int32_t T = static_cast<int32_t>((4.0f * t + 3.0f) * static_cast<float>(kCinematicFps));
        const ObjectFrame& f = o.frame;
        const auto rot = [&f](const Vec3& v) { return f.axis[0] * v.x + f.axis[1] * v.y + f.axis[2] * v.z; };
        ShotKey a = key(0, ShotKey::Segment::Spline, true, rot({2.0f * e.x, 2.0f * sgn * e.y, 2.0f * e.z}), 0.25f);
        a.translate_only = true;
        ShotKey b = key(T, ShotKey::Segment::Spline, true, rot({2.0f * e.x, -2.0f * sgn * e.y, 2.0f * e.z}), 0.0f, 0.25f);
        b.translate_only = true;
        s.eye.push_back(a);
        s.eye.push_back(b);
        s.look.push_back(key(0, ShotKey::Segment::Hold, true, {}, 0.0f, 0.25f));
        s.length = T;
    }
    out = std::move(s);
    return true;
}

bool push_in_shot(const InterestingObject&, UnsyncedRandom&, BattleCamShot&) { return false; }
bool drive_by_shot(const InterestingObject&, UnsyncedRandom&, BattleCamShot&) { return false; }
bool land_flying_shot(const InterestingObject&, UnsyncedRandom&, BattleCamShot&) { return false; }

bool interesting_shot(const InterestingObject& o, bool space, const InterestingCategories& c, UnsyncedRandom& rng,
                      BattleCamShot& out) {
    // Steps 1 - 5 (docs/re/BATTLE_CAM.md "The dispatcher"): the glory files (#572), the hardpoint shot (#571), the
    // fighter chase (#570) and the infantry push-in (#569) are not ported: they fail and fall through. Step 2 (the
    // terrain clamp) is BattleCam::start's.
    if (rng.range(0, 3) == 0) {  // step 6: the static shot for a quarter of what gets here
        out = static_shot(o, space, rng);
        return true;
    }
    if ((o.category & (c.corvette | c.bomber)) != 0) (void)rng.range(0, 1);  // step 7's coin: attack run / chase (#570)
    // Steps 8 (frigate: the attack run), 9 (vehicle: the drive-by), 10 (no locomotor: the structure shot) and 11 (flying,
    // land by size) name templates that are not ported yet (#569, #570): they fall through to step 12.
    if (rng.range(0, 1) == 0) {  // step 12: the orbit or the fly-past
        out = orbit_shot(o, rng);
        return true;
    }
    return fly_past_shot(o, space, rng, out);
}

}  // namespace alamo::game
