// alamo_game_battle_cam_tests (data-free): the tactical battle-cam controller (#462, docs/re/BATTLE_CAM.md;
// battle_cam.hpp). Covered:
// - the key evaluator's orbit after the last key (0x49c810): the angle at the hold end, a quarter turn, a full turn, both
//   directions, and the look held on the object;
// - the hero clash shot (0x4a4ac0): the radius clamps and the keys;
// - Cinematic View (0x4a6790): yaw and pitch per view, the distance clamp, the offset;
// - the state machine (0x2cbd00 / 0x2cc080 / 0x2cca80 / 0x2cc3e0 / 0x2cbc40): Movie Mode on key up and its gates, the
//   next clash, Esc, Cinematic View's steps and key rules, the mouse rules, the smoothing.
#include <cmath>
#include <cstdio>
#include <optional>
#include <string_view>
#include <vector>

#include "alamo/game/battle_cam.hpp"
#include "alamo/game/battle_fx.hpp"
#include "alamo/test/support.hpp"


using namespace alamo::game;
using ::alamo::test::g_fail;
using V3 = ::alamo::tactical::Vec3;
using State = BattleCam::State;
using Key = BattleCam::Key;

namespace {

bool near(float a, float b, float eps = 1e-3f) { return std::fabs(a - b) <= eps; }
bool near(const V3& a, const V3& b, float eps = 1e-2f) { return near(a.x, b.x, eps) && near(a.y, b.y, eps) && near(a.z, b.z, eps); }

ObjectFrame frame_at(V3 p) {
    ObjectFrame f;
    f.valid = true;
    f.position = p;
    return f;
}

void test_orbit() {
    // The hero clash keys on a hero at (100, 50, 0), E = (20, 20, 20): R = 150 (7.5 x 20, inside 125..500).
    const BattleCamShot s = hero_clash_shot(7, 1, {20.0f, 20.0f, 20.0f}, false, false);
    CHECK(near(hero_clash_radius({20.0f, 20.0f, 20.0f}, false), 150.0f));
    CHECK(near(hero_clash_radius({1.0f, 1.0f, 1.0f}, false), 125.0f));      // under the floor: the floor
    CHECK(near(hero_clash_radius({100.0f, 100.0f, 100.0f}, false), 500.0f));  // over the cap: the cap
    CHECK(near(hero_clash_radius({1.0f, 1.0f, 1.0f}, true), 250.0f));        // mode type 2: 250..1000
    CHECK(near(hero_clash_radius({200.0f, 200.0f, 200.0f}, true), 1000.0f));
    CHECK(s.eye.size() == 2 && s.look.size() == 1 && s.target == 7 && s.clash == 1);
    CHECK(s.eye.size() == 2 && s.eye[1].frame == 45 && s.eye[1].segment == ShotKey::Segment::Orbit && s.eye[1].period == 600);
    const ObjectFrame f = frame_at({100.0f, 50.0f, 0.0f});
    const float z = 6.0f;
    const V3 look = shot_keys_look(s.look, 100, f, z);
    CHECK(near(look, {100.0f, 50.0f, 6.0f}));
    // Frames 0..45: held at (0, R, 0.67R) in the hero's frame (+ the z adjust).
    const V3 held{100.0f, 200.0f, 100.5f + 6.0f};
    CHECK(near(shot_keys_eye(s.eye, s.look, 0, f, z), held));
    CHECK(near(shot_keys_eye(s.eye, s.look, 45, f, z), held));
    // A quarter turn (150 frames after 45) counter-clockwise about +Z: (0, R) -> (-R, 0).
    CHECK(near(shot_keys_eye(s.eye, s.look, 195, f, z), {-50.0f, 50.0f, 106.5f}));
    // A full turn (600 frames) is back at the start.
    CHECK(near(shot_keys_eye(s.eye, s.look, 645, f, z), held));
    // reverse: a quarter turn clockwise, (0, R) -> (R, 0).
    const BattleCamShot r = hero_clash_shot(7, 1, {20.0f, 20.0f, 20.0f}, false, true);
    CHECK(near(shot_keys_eye(r.eye, r.look, 195, f, z), {250.0f, 50.0f, 106.5f}));
    // The circle follows the hero's facing: rotated 90 degrees, the held eye is on its new +Y side.
    ObjectFrame turned = f;
    turned.axis[0] = {0.0f, 1.0f, 0.0f};
    turned.axis[1] = {-1.0f, 0.0f, 0.0f};
    CHECK(near(shot_keys_eye(s.eye, s.look, 10, turned, z), {-50.0f, 50.0f, 106.5f}));
    // A non-orbit last key is held, and a look Orbit segment yields (0, 0, 0) (0x49de50 has no mode-3 branch).
    std::vector<ShotKey> lk(2);
    lk[0].segment = ShotKey::Segment::Orbit;
    lk[0].attached = true;
    lk[1].frame = 100;
    CHECK(near(shot_keys_look(lk, 50, f, z), {0.0f, 0.0f, 0.0f}));
}

void test_views() {
    CHECK(near(cinematic_view_yaw(0), 0.0f) && near(cinematic_view_yaw(1), 315.0f) && near(cinematic_view_yaw(2), 270.0f));
    CHECK(near(cinematic_view_yaw(7), 45.0f) && near(cinematic_view_yaw(8), 0.0f));
    CHECK(near(cinematic_view_pitch(true), 30.0f) && near(cinematic_view_pitch(false), 45.0f));
    // d = clamp(3 x avg(E), 3|E|, 10|E|): a cube gives 3 x 10 = 30 under 3|E| = 51.96: the floor.
    const V3 cube{10.0f, 10.0f, 10.0f};
    CHECK(near(cinematic_view_distance(cube), 3.0f * std::sqrt(300.0f)));
    CHECK(near(cinematic_view_distance(cube, 3.0f), 90.0f));           // inside the band
    CHECK(near(cinematic_view_distance(cube, 100.0f), 10.0f * std::sqrt(300.0f)));  // the cap
    // The offset: view 0 on the object's +Y side, up by the pitch; yaw 90 on its -X side.
    CHECK(near(cinematic_view_offset(0.0f, 30.0f, 100.0f), {0.0f, 86.6025f, 50.0f}));
    CHECK(near(cinematic_view_offset(90.0f, 0.0f, 100.0f), {-100.0f, 0.0f, 0.0f}));
    const BattleCamShot s = cinematic_view_shot(9, cube, 0.0f, 45.0f, 3.0f);
    CHECK(s.target == 9 && s.clash == 0 && s.eye.size() == 1 && s.look.size() == 1 && s.smoothing && near(*s.smoothing, 0.9f));
    CHECK(!s.eye.empty() && s.eye[0].attached && s.eye[0].segment == ShotKey::Segment::Linear);
    // One linear key each: held, and attached, so it follows the object.
    const ObjectFrame f = frame_at({0.0f, 0.0f, 0.0f});
    CHECK(near(shot_keys_eye(s.eye, s.look, 500, f, 0.0f), {0.0f, 63.6396f, 63.6396f}));
}

// A world of picks the tests drive: a selection that can be viewed, a clash list of ids 1..n.
struct World {
    bool selection = true;
    std::vector<uint64_t> clashes;
    int view_calls = 0;
    // #568: the interesting-object shots handed out in turn (empty: none), and the calls' `first`.
    std::vector<BattleCamShot> interesting;
    std::vector<bool> interesting_first;
    uint32_t last_view_target = 0xFFFFFFFFu;
    BattleCamPicks picks() {
        BattleCamPicks p;
        p.view = [this](uint32_t target, uint32_t& id, V3& half_extents) {
            ++view_calls;
            last_view_target = target;
            if (target == 0 && !selection) return false;
            id = target != 0 ? target : 9;
            half_extents = {10.0f, 10.0f, 10.0f};
            return true;
        };
        p.interesting = [this](bool first, BattleCamShot& out) {
            interesting_first.push_back(first);
            if (interesting.empty()) return false;
            out = interesting.front();
            interesting.erase(interesting.begin());
            return true;
        };
        p.clash = [this](uint64_t after, BattleCamShot& out) {
            if (clashes.empty()) return false;
            size_t pick = 0;
            for (size_t i = 0; i < clashes.size(); ++i)
                if (clashes[i] == after) {
                    if (clashes.size() == 1) return false;
                    pick = (i + 1) % clashes.size();
                }
            out = hero_clash_shot(static_cast<uint32_t>(100 + clashes[pick]), clashes[pick], {20.0f, 20.0f, 20.0f}, false, false);
            return true;
        };
        return p;
    }
};

void test_movie_mode() {
    World w;
    BattleCamGates g;
    BattleCam cam;
    // No clash: Movie Mode frames nothing ([port] the bombing run / interesting-object picks are not ported).
    CHECK(!cam.movie_key_up(g, w.picks()) && !cam.active());
    w.clashes = {1, 2};
    // The gates: ENABLE_COMBAT_CINEMATIC off or LOCK_CONTROLS on, nothing; In_Game_Cinematics off, no shot.
    BattleCamGates off = g;
    off.combat_cinematic = false;
    CHECK(!cam.movie_key_up(off, w.picks()) && !cam.active());
    off = g;
    off.controls_locked = true;
    CHECK(!cam.movie_key_up(off, w.picks()) && !cam.active());
    off = g;
    off.in_game_cinematics = false;
    CHECK(!cam.movie_key_up(off, w.picks()) && !cam.active());
    // Key up: the first clash.
    CHECK(cam.movie_key_up(g, w.picks()) && cam.state() == State::Movie && cam.shot().clash == 1 && cam.shot().target == 101);
    const uint32_t s1 = cam.serial();
    // Held key down in state 3: swallowed, the hold set; key up: the next clash (wrapping), never the same one.
    CHECK(cam.key_down(Key::MovieMode, g, w.picks()) && cam.key_held());
    CHECK(cam.movie_key_up(g, w.picks()) && !cam.key_held() && cam.shot().clash == 2 && cam.serial() == s1 + 1);
    CHECK(cam.movie_key_up(g, w.picks()) && cam.shot().clash == 1);
    w.clashes = {1};
    CHECK(!cam.movie_key_up(g, w.picks()) && cam.state() == State::Movie && cam.shot().clash == 1);  // the shot runs on
    // Other keys pass; Esc stops.
    CHECK(!cam.key_down(Key::Other, g, w.picks()) && cam.active());
    CHECK(!cam.key_down(Key::Tether, g, w.picks()) && cam.active());
    CHECK(cam.key_down(Key::Escape, g, w.picks()) && !cam.active());
    // The clash ends: the next one, else stop.
    w.clashes = {1, 2};
    CHECK(cam.movie_key_up(g, w.picks()) && cam.shot().clash == 1);
    w.clashes = {2};
    cam.clash_ended(w.picks());
    CHECK(cam.state() == State::Movie && cam.shot().clash == 2);
    w.clashes.clear();
    cam.clash_ended(w.picks());
    CHECK(!cam.active());
    // Esc with nothing running passes through (the pause menu keeps it).
    CHECK(!cam.key_down(Key::Escape, g, w.picks()));
}

void test_mouse() {
    World w;
    w.clashes = {1};
    BattleCamGates g;
    BattleCam cam;
    CHECK(cam.movie_key_up(g, w.picks()));
    // 1920 x 1080: under 96 x 54 pixels per move is swallowed, the baseline follows every move, so slow drags never stop it.
    cam.mouse(500, 500, 1920, 1080, false, 0.0f, w.picks());
    cam.mouse(590, 540, 1920, 1080, false, 0.0f, w.picks());
    cam.mouse(680, 590, 1920, 1080, false, 0.0f, w.picks());
    CHECK(cam.active());
    cam.mouse(780, 590, 1920, 1080, false, 0.0f, w.picks());  // 100 px
    CHECK(!cam.active());
    CHECK(cam.movie_key_up(g, w.picks()));
    cam.mouse(780, 590, 1920, 1080, true, 0.0f, w.picks());  // a button
    CHECK(!cam.active());
    CHECK(cam.movie_key_up(g, w.picks()));
    cam.mouse(780, 590, 1920, 1080, false, 1.0f, w.picks());  // the wheel
    CHECK(!cam.active());
    // Cinematic View: the dominant axis / 4 turns the view; pitch clamped to +-80, yaw wrapped; buttons do not end it.
    CHECK(cam.key_down(Key::CinematicView, g, w.picks()) && cam.state() == State::View);
    cam.mouse(100, 100, 1920, 1080, false, 0.0f, w.picks());
    cam.mouse(140, 110, 1920, 1080, false, 0.0f, w.picks());  // dx 40 dominant: yaw -= 10
    CHECK(near(cam.view_yaw(), 350.0f) && near(cam.view_pitch(), 45.0f));
    cam.mouse(140, 400, 1920, 1080, true, 0.0f, w.picks());  // dy 290: pitch += 72.5, clamped
    CHECK(cam.state() == State::View && near(cam.view_pitch(), 80.0f) && near(cam.view_yaw(), 350.0f));
    CHECK(w.last_view_target == 9 && cam.shot().target == 9);  // the turn rebuilds the framed object's view
    CHECK(!cam.shot().eye.empty() && cam.shot().eye[0].where.z > 0.0f);  // the eye key rebuilt high above
}

void test_cinematic_view() {
    World w;
    BattleCamGates g;
    g.space = true;
    BattleCam cam;
    // In_Game_Cinematics off: the key is swallowed and nothing opens.
    BattleCamGates off = g;
    off.in_game_cinematics = false;
    CHECK(cam.key_down(Key::CinematicView, off, w.picks()) && !cam.active());
    // Nothing selected: swallowed, nothing opens.
    w.selection = false;
    CHECK(cam.key_down(Key::CinematicView, g, w.picks()) && !cam.active());
    w.selection = true;
    CHECK(cam.key_down(Key::CinematicView, g, w.picks()) && cam.state() == State::View && cam.view() == 0);
    CHECK(near(cam.view_yaw(), 0.0f) && near(cam.view_pitch(), 30.0f));
    // View 0: other keys pass; the Movie Mode key passes too.
    CHECK(!cam.key_down(Key::Other, g, w.picks()) && cam.active());
    CHECK(!cam.key_down(Key::MovieMode, g, w.picks()) && cam.active());
    // Steps through the 8 views, wrapping after the last.
    for (int i = 1; i <= 7; ++i) CHECK(cam.key_down(Key::CinematicView, g, w.picks()) && cam.view() == i);
    CHECK(near(cam.view_yaw(), 45.0f));
    CHECK(cam.key_down(Key::CinematicView, g, w.picks()) && cam.view() == 0);
    CHECK(cam.key_down(Key::CinematicView, g, w.picks()) && cam.view() == 1);
    // A view above 0: Ctrl is swallowed and keeps it; any other key stops and is swallowed.
    CHECK(cam.key_down(Key::Control, g, w.picks()) && cam.active());
    CHECK(cam.key_down(Key::Other, g, w.picks()) && !cam.active());
    // In a view, the Movie Mode key up stops the cam.
    CHECK(cam.key_down(Key::CinematicView, g, w.picks()) && cam.state() == State::View);
    CHECK(cam.movie_key_up(g, w.picks()) && !cam.active());
    // Esc stops view 0.
    CHECK(cam.key_down(Key::CinematicView, g, w.picks()) && cam.key_down(Key::Escape, g, w.picks()) && !cam.active());
    // [port] the selection emptied: the next view step stops the cam.
    CHECK(cam.key_down(Key::CinematicView, g, w.picks()));
    w.selection = false;
    CHECK(cam.key_down(Key::CinematicView, g, w.picks()) && !cam.active());
}

// #564: the wheel zoom (0x4ac520), the sticky terrain clamp (+0x315), the handheld noise (0x4ab780).
void test_polish() {
    World w;
    BattleCamGates g;
    BattleCam cam;
    CHECK(near(cam.zoom(), 1.0f) && !cam.terrain_clamp());
    // E = (10, 10, 10): 3 avg(E) = 30, |E| = 17.32, so d in [51.96, 173.2].
    CHECK(cam.key_down(Key::CinematicView, g, w.picks()) && cam.terrain_clamp());
    const float lo = 3.0f * std::sqrt(300.0f), hi = 10.0f * std::sqrt(300.0f);
    // The first notch from a fresh view is computed from 30 (unclamped) and clamped back to 3|E|: the factor jumps there.
    cam.mouse(100, 100, 1920, 1080, false, 1.0f, w.picks());
    CHECK(cam.state() == State::View && near(cam.zoom(), lo / 30.0f));
    // Wheel towards the user (-): 25 units further per notch.
    cam.mouse(100, 100, 1920, 1080, false, -2.0f, w.picks());
    CHECK(near(cam.zoom(), (lo + 50.0f) / 30.0f));
    const V3 eye = cam.shot().eye.empty() ? V3{} : cam.shot().eye[0].where;
    CHECK(near(std::sqrt(::alamo::tactical::dot(eye, eye)), lo + 50.0f, 0.01f));  // the eye key rebuilt at the new distance
    cam.mouse(100, 100, 1920, 1080, false, -100.0f, w.picks());
    CHECK(near(cam.zoom(), hi / 30.0f));  // clamped to 10|E|
    // The factor survives the stop and the next view; the clamp flag survives too.
    const float z = cam.zoom();
    cam.stop();
    CHECK(near(cam.zoom(), z) && cam.terrain_clamp());
    CHECK(cam.key_down(Key::CinematicView, g, w.picks()));
    CHECK(!cam.shot().eye.empty() && near(std::sqrt(::alamo::tactical::dot(cam.shot().eye[0].where, cam.shot().eye[0].where)), hi, 0.01f));
    // The terrain clamp: 15 over the highest of the 9 samples at +-15; an eye already higher is kept.
    const auto ground = [](float x, float y) { return x > 110.0f ? 80.0f : (y < -5.0f ? 40.0f : 0.0f); };
    CHECK(near(terrain_clamped_eye({100.0f, 0.0f, 10.0f}, ground), {100.0f, 0.0f, 95.0f}));   // the x + 15 column: 80 + 15
    CHECK(near(terrain_clamped_eye({0.0f, 0.0f, 10.0f}, ground), {0.0f, 0.0f, 55.0f}));       // the y - 15 row: 40 + 15
    CHECK(near(terrain_clamped_eye({0.0f, 100.0f, 10.0f}, ground), {0.0f, 100.0f, 15.0f}));   // flat: 0 + 15
    CHECK(near(terrain_clamped_eye({0.0f, 100.0f, 30.0f}, ground), {0.0f, 100.0f, 30.0f}));
    CHECK(near(terrain_clamped_eye({0.0f, 0.0f, 10.0f}, {}), {0.0f, 0.0f, 10.0f}));             // no terrain
    // The handheld noise: both shots carry it; its parameters.
    CHECK(cam.shot().handheld);
    CHECK(hero_clash_shot(1, 1, {20.0f, 20.0f, 20.0f}, false, false).handheld);
    const ShakeEffect e = handheld_shake(kHandheldFrames);
    CHECK(near(e.x_amplitude, 20.0f) && near(e.y_amplitude, 20.0f) && near(e.roll_amplitude, 0.0f) && near(e.noise_offset, 0.0f));
    CHECK(near(e.ease_in, 0.0f) && near(e.ease_out, 0.1f));
    CHECK(near(e.frequency, 0.01f * static_cast<float>(kHandheldFrames), 1.0f));
    CHECK(near(handheld_shake(600).frequency, 6.0f));
    // Over a shot: s = 0.01 x the frames since the start, so 100 frames = one noise cell; it moves, without roll.
    CameraShake shake;
    CHECK(shake.start(handheld_shake(kHandheldFrames), 0, kHandheldFrames));
    const CameraShake::Sample a = shake.evaluate(150, {1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f});
    CHECK(near(a.roll_deg, 0.0f) && (a.offset.x != 0.0f || a.offset.z != 0.0f));
    CHECK(std::fabs(a.offset.x) <= 40.0f && std::fabs(a.offset.z) <= 40.0f && near(a.offset.y, 0.0f));
}

// #568: the ranking (0x520df0), the pick (0x2cd0d0 with the +0x368 alternation), the templates and the chain.
SaliencyFeatures feat(float power = 0.0f, float health = 0.0f, float x = 0.0f) {
    SaliencyFeatures f;
    f.power = power;
    f.health = health;
    f.x = x;
    return f;
}

void test_saliency_rank() {
    const SaliencyWeights w;
    const std::vector<SaliencyFeatures> by_power = {feat(1.0f), feat(3.0f), feat(2.0f)};
    CHECK((saliency_rank(by_power, w) == std::vector<size_t>{1, 2, 0}));
    const std::vector<SaliencyFeatures> by_health = {feat(0.0f, 10.0f), feat(0.0f, 5.0f)};
    CHECK((saliency_rank(by_health, w) == std::vector<size_t>{1, 0}));  // lower health is more salient
    const std::vector<SaliencyFeatures> by_x = {feat(0.0f, 0.0f, 0.0f), feat(0.0f, 0.0f, 5.0f), feat(0.0f, 0.0f, 10.0f)};
    CHECK(saliency_rank(by_x, w).front() == 1);  // nearest the mean
    // The spread weighting: power spread 1 x weight 1 against health spread 1 x weight 1.5 (an override).
    SaliencyWeights hw = w;
    hw.health = 1.5f;
    const std::vector<SaliencyFeatures> both = {feat(1.0f, 10.0f), feat(0.0f, 0.0f)};
    CHECK((saliency_rank(both, w) == std::vector<size_t>{0, 1}));   // tie 1 : 1 keeps the order
    CHECK((saliency_rank(both, hw) == std::vector<size_t>{1, 0}));  // health wins
    CHECK(saliency_rank({}, w).empty());
}

InterestingWorld world3() {
    InterestingWorld w;
    w.type_of = [](uint32_t id) { return id >= 20 && id < 40 ? static_cast<int32_t>(id % 3) : id == 99 ? 7 : -1; };
    w.may_follow = [](uint32_t) { return true; };
    w.target = [](uint32_t) { return 0u; };
    w.candidate_ids = {20, 21, 22};
    w.candidate_types = {2, 0, 1};
    w.candidate_features = {feat(1.0f), feat(3.0f), feat(2.0f)};
    return w;
}

void test_interesting_pick() {
    UnsyncedRandom rng(5);
    InterestingWorld w = world3();
    InterestingState s;
    // first: a random object of the selection, no filter; the stored id is left alone.
    w.selection = {7, 8};
    w.type_of = [](uint32_t id) { return static_cast<int32_t>(id); };
    CHECK(interesting_pick(s, true, w, rng) && (s.current == 7 || s.current == 8) && s.type == static_cast<int32_t>(s.current));
    CHECK(s.stored == 0);
    w = world3();
    // A ranked pick that differs from the last type; with every candidate of that type, the first ranked one.
    s = {};
    CHECK(interesting_pick(s, false, w, rng) && (s.current >= 20 && s.current <= 22));
    s = {};
    s.type = 5;
    w.candidate_types = {5, 5, 5};
    CHECK(interesting_pick(s, false, w, rng) && s.current == 21);  // ranked first (power 3)
    w = world3();
    // The validation: a stored id that is not the current object clears it (+0x368), so no target branch.
    w.target = [](uint32_t) { return 99u; };
    bool followed = false, ranked = false;
    for (uint64_t seed = 1; seed <= 40; ++seed) {
        UnsyncedRandom r(seed);
        InterestingState a;
        a.current = 30;
        a.stored = 31;  // the previous object's id: the retail bug's second step
        CHECK(interesting_pick(a, false, w, r) && a.current != 99 && a.stored == 0);
        InterestingState b;
        b.current = 30;  // stored 0: armed from the current object, validation passes
        CHECK(interesting_pick(b, false, w, r) && b.stored == 30);
        (b.current == 99 ? followed : ranked) = true;
        if (b.current == 99) CHECK(b.type == 7);
    }
    CHECK(followed && ranked);  // the target 2/3 of the time, else a ranked candidate
    // A gone current object is cleared; nothing at all = false.
    InterestingState g;
    g.current = 5;  // type_of(5) = -1
    w.candidate_ids.clear();
    w.candidate_types.clear();
    w.candidate_features.clear();
    CHECK(!interesting_pick(g, false, w, rng) && g.current == 0);
}

InterestingObject object(V3 e, bool model = true, std::string_view name = "Corvette_X") {
    InterestingObject o;
    o.id = 42;
    o.half_extents = e;
    o.model = model;
    o.type_name = name;
    o.frame = frame_at({100.0f, 50.0f, 0.0f});
    return o;
}

void test_interesting_templates() {
    UnsyncedRandom rng(9);
    // The orbit: radius 2|E| (either side), height 1.25 Ez, translate-only, 600-frame turn, length 90 + 180 t.
    const BattleCamShot o = orbit_shot(object({10.0f, 10.0f, 10.0f}), rng);
    CHECK(o.interesting && o.target == 42 && o.length == 90 && o.smoothing && near(*o.smoothing, 0.9f) && !o.handheld);
    CHECK(o.eye.size() == 1 && o.look.size() == 1);
    if (o.eye.size() == 1 && o.look.size() == 1) {
        const ShotKey& e = o.eye[0];
        CHECK(e.segment == ShotKey::Segment::Orbit && e.translate_only && e.period == 600 && near(e.ease_out, 0.25f));
        CHECK(near(std::fabs(e.where.x), 2.0f * std::sqrt(300.0f)) && near(e.where.z, 12.5f) && o.look[0].translate_only);
        // Translate-only: the offset is not turned by the object's frame.
        ObjectFrame turned = frame_at({100.0f, 50.0f, 0.0f});
        turned.axis[0] = {0.0f, 1.0f, 0.0f};
        turned.axis[1] = {-1.0f, 0.0f, 0.0f};
        CHECK(near(shot_keys_eye(o.eye, o.look, 0, turned, 0.0f), V3{100.0f + e.where.x, 50.0f, 12.5f}));
    }
    CHECK(orbit_shot(object({300.0f, 300.0f, 300.0f}), rng).length == 270);  // |E| >= 500: t = 1
    // The static shot: world eye and look, 60 frames, handheld over 60; d in [2|E|, 4|E|], up 20..45 degrees on land.
    const BattleCamShot st = static_shot(object({10.0f, 10.0f, 10.0f}), false, rng);
    CHECK(st.length == 60 && st.handheld && st.handheld_frames == 60 && st.interesting && !st.smoothing);
    if (st.eye.size() == 1 && st.look.size() == 1) {
        CHECK(!st.eye[0].attached && !st.look[0].attached && near(st.look[0].where, V3{100.0f, 50.0f, 0.0f}));
        const V3 d = st.eye[0].where - st.look[0].where;
        const float len = std::sqrt(::alamo::tactical::dot(d, d)), e = std::sqrt(300.0f);
        CHECK(len >= 2.0f * e - 0.01f && len <= 4.0f * e + 0.01f);
        const float up = std::asin(d.z / len) * 180.0f / 3.14159265f;
        CHECK(up >= 19.9f && up <= 45.1f);
    } else {
        CHECK(false);
    }
    // The fly-past: needs a model; the two frigates never get variant A; A only in space.
    BattleCamShot fp;
    CHECK(!fly_past_shot(object({10.0f, 10.0f, 10.0f}, false), true, rng, fp));
    bool a = false, b = false;
    for (int i = 0; i < 40; ++i) {
        CHECK(fly_past_shot(object({10.0f, 10.0f, 10.0f}), true, rng, fp) && fp.smoothing && near(*fp.smoothing, 0.9f));
        const bool is_b = !fp.eye.empty() && fp.eye[0].translate_only;
        (is_b ? b : a) = true;
        CHECK(is_b ? fp.length == 90 : fp.length == 240);  // t = 0: B 3 s, A 8 s
        CHECK(fly_past_shot(object({10.0f, 10.0f, 10.0f}, true, "nebulon_b_frigate"), true, rng, fp) && fp.eye[0].translate_only);
        CHECK(fly_past_shot(object({10.0f, 10.0f, 10.0f}), false, rng, fp) && fp.eye[0].translate_only);  // land: B
    }
    CHECK(a && b);
    // The dispatcher: with a model it always finds a shot (static, orbit or fly-past here).
    bool saw_static = false, saw_orbit = false, saw_fly = false;
    for (int i = 0; i < 60; ++i) {
        BattleCamShot s;
        CHECK(interesting_shot(object({10.0f, 10.0f, 10.0f}), true, InterestingCategories{}, rng, s) && s.interesting);
        if (s.length == 60) saw_static = true;
        else if (!s.eye.empty() && s.eye[0].segment == ShotKey::Segment::Orbit) saw_orbit = true;
        else saw_fly = true;
    }
    CHECK(saw_static && saw_orbit && saw_fly);
}

// #569: the ground templates. The object sits at (100, 50, 0), turned 90 degrees (its +X is world +Y).
InterestingObject ground(V3 e, bool locomotor = true, bool flying = false) {
    InterestingObject o = object(e);
    o.locomotor = locomotor;
    o.flying = flying;
    o.frame.axis[0] = {0.0f, 1.0f, 0.0f};
    o.frame.axis[1] = {-1.0f, 0.0f, 0.0f};
    return o;
}

void test_ground_templates() {
    UnsyncedRandom rng(5);
    const InterestingObject o = ground({4.0f, 2.0f, 10.0f});
    // The push-in: world eye keys from the start's frame, attached look keys, n = 120..150, h = 1.5..2.0, noise over n.
    for (int i = 0; i < 20; ++i) {
        BattleCamShot s;
        CHECK(push_in_shot(o, rng, s) && s.interesting && s.target == 42 && !s.smoothing);
        CHECK(s.length >= 120 && s.length <= 150 && s.handheld && s.handheld_frames == s.length);
        if (s.eye.size() != 2 || s.look.size() != 2) {
            CHECK(false);
            continue;
        }
        CHECK(!s.eye[0].attached && !s.eye[1].attached && s.look[0].attached && s.look[1].attached);
        CHECK(s.eye[0].frame == 0 && s.eye[1].frame == s.length && s.look[1].frame == s.length);
        CHECK(s.eye[0].segment == ShotKey::Segment::Spline && near(s.eye[0].ease_out, 0.25f) && near(s.eye[1].ease_in, 0.25f));
        // Eye 1 = M (2s Ex, 2 Ey, 2h Ez): x = 100 - 2 Ey = 96, y = 50 + 2s Ex = 50 +- 8, z = 2h Ez = 30..40.
        const V3 a = s.eye[0].where, b = s.eye[1].where;
        CHECK(near(a.x, 96.0f) && near(std::fabs(a.y - 50.0f), 8.0f) && a.z >= 29.99f && a.z <= 40.01f);
        const float h = a.z / 20.0f;  // 2h Ez = 20 h
        CHECK(near(b.x, 96.0f) && near(std::fabs(b.y - 50.0f), 4.0f) && near(b.z, h * 10.0f));
        CHECK(near(s.look[0].where, V3{0.0f, 0.0f, 0.25f * h * 10.0f}) && near(s.look[1].where, V3{0.0f, -4.0f, 0.1f * h * 10.0f}));
    }
    BattleCamShot none;
    CHECK(!push_in_shot(object({4.0f, 2.0f, 10.0f}, false), rng, none));
    // The drive-by: every eye key a world point; length 2n (120..180); three variants; no noise, smoothing inherited.
    bool seen[3] = {false, false, false};
    for (int i = 0; i < 60; ++i) {
        BattleCamShot s;
        CHECK(drive_by_shot(o, rng, s) && !s.handheld && !s.smoothing);
        CHECK(s.length >= 120 && s.length <= 180 && s.length % 2 == 0);
        if (s.eye.size() != 2 || s.look.size() != 2) {
            CHECK(false);
            continue;
        }
        CHECK(!s.eye[0].attached && !s.eye[1].attached && s.eye[1].frame == s.length && near(s.eye[1].ease_in, 0.25f));
        if (!s.look[0].attached) {  // variant 2: 7 up, the look on M (0, 0, 30) -> M (0, 0, 20)
            seen[2] = true;
            CHECK(near(s.eye[0].where.z, 7.0f) && near(s.eye[1].where.z, 7.0f) && near(s.eye[0].where.x, 106.0f));
            CHECK(near(s.look[0].where, V3{100.0f, 50.0f, 30.0f}) && near(s.look[1].where, V3{100.0f, 50.0f, 20.0f}));
            CHECK(near(s.look[1].ease_out, 0.25f));
        } else if (near(s.look[0].where, V3{0.0f, -2.0f, 0.0f})) {  // variant 1: from behind, H = clamp(5 Ez, 50, 125)
            seen[1] = true;
            CHECK(near(s.eye[0].where, V3{106.0f, s.eye[0].where.y, 50.0f}) && near(std::fabs(s.eye[0].where.y - 50.0f), 12.0f));
            CHECK(near(s.eye[1].where.z, 10.0f) && near(s.look[1].where, V3{}) && near(s.look[1].ease_in, 0.25f));
        } else {  // variant 0: from ahead, the look drifts to (0, -3 Ey, 0)
            seen[0] = true;
            CHECK(near(s.eye[0].where.x, 96.0f) && near(s.eye[0].where.z, 50.0f) && near(s.eye[1].where.x, 100.0f));
            CHECK(near(s.look[0].where, V3{}) && near(s.look[1].where, V3{0.0f, -6.0f, 0.0f}));
        }
    }
    CHECK(seen[0] && seen[1] && seen[2]);
    InterestingObject tall = ground({4.0f, 2.0f, 40.0f});
    BattleCamShot t;
    for (int i = 0; i < 20; ++i)
        if (drive_by_shot(tall, rng, t) && t.look[0].attached) CHECK(near(t.eye[0].where.z, 125.0f));  // H capped at 125
    // The land flyer: a fixed world eye beside it, the look held on it, n = 60..90.
    BattleCamShot f;
    CHECK(land_flying_shot(ground({4.0f, 2.0f, 10.0f}, true, true), rng, f) && f.length >= 60 && f.length <= 90 && !f.handheld);
    if (f.eye.size() == 1 && f.look.size() == 1) {
        CHECK(f.eye[0].segment == ShotKey::Segment::Hold && !f.eye[0].attached && near(f.eye[0].where.x, 102.0f));
        CHECK(near(std::fabs(f.eye[0].where.y - 50.0f), 4.0f) && near(f.eye[0].where.z, 10.0f));
        CHECK(f.look[0].attached && f.look[0].segment == ShotKey::Segment::Hold && near(f.look[0].ease_out, 0.25f));
    } else {
        CHECK(false);
    }
}

void test_ground_dispatch() {
    UnsyncedRandom rng(11);
    InterestingCategories c;
    const auto infantry = [&] {
        InterestingObject o = ground({4.0f, 2.0f, 10.0f});
        o.category = c.infantry;
        return o;
    };
    for (int i = 0; i < 20; ++i) {
        BattleCamShot s;  // step 5: infantry always takes the push-in (it never fails with a model)
        CHECK(interesting_shot(infantry(), false, c, rng, s) && s.handheld && s.length >= 120 && s.length <= 150 &&
              s.eye.size() == 2 && !s.eye[0].attached);
    }
    // A vehicle: the static shot a quarter of the time (step 6), otherwise the drive-by (step 9).
    bool saw_static = false, saw_drive = false;
    for (int i = 0; i < 60; ++i) {
        InterestingObject o = ground({40.0f, 20.0f, 10.0f});
        o.category = c.vehicle;
        BattleCamShot s;
        CHECK(interesting_shot(o, false, c, rng, s));
        if (s.length == 60 && s.eye.size() == 1) saw_static = true;
        else if (s.eye.size() == 2 && !s.eye[0].attached && !s.handheld) saw_drive = true;
        else CHECK(false);
    }
    CHECK(saw_static && saw_drive);
    // Step 11 on land, no category: a flyer gets the flying shot; small (|E| < 30) the push-in, large the drive-by.
    bool fly = false, push = false, drive = false;
    for (int i = 0; i < 60; ++i) {
        BattleCamShot s;
        CHECK(interesting_shot(ground({4.0f, 2.0f, 10.0f}, true, true), false, c, rng, s));
        if (s.eye.size() == 1 && s.eye[0].segment == ShotKey::Segment::Hold && !s.eye[0].attached && s.length != 60) fly = true;
        CHECK(interesting_shot(ground({4.0f, 2.0f, 10.0f}), false, c, rng, s));
        if (s.handheld && s.length >= 120) push = true;
        CHECK(interesting_shot(ground({40.0f, 20.0f, 10.0f}), false, c, rng, s));
        if (!s.handheld && s.eye.size() == 2 && !s.eye[0].attached) drive = true;
        // In space the size test is skipped: the orbit or the fly-past.
        CHECK(interesting_shot(ground({4.0f, 2.0f, 10.0f}), true, c, rng, s));
        CHECK(s.length == 60 || (!s.eye.empty() && (s.eye[0].segment == ShotKey::Segment::Orbit || s.smoothing)));
    }
    CHECK(fly && push && drive);
    // Without a model a land object falls through to the orbit (step 12's fly-past needs a model too).
    for (int i = 0; i < 20; ++i) {
        InterestingObject o = ground({4.0f, 2.0f, 10.0f});
        o.model = false;
        BattleCamShot s;
        if (interesting_shot(o, false, c, rng, s)) CHECK(s.length == 60 || s.eye[0].segment == ShotKey::Segment::Orbit);
    }
}

BattleCamShot timed(uint32_t id, int32_t length, std::optional<float> smoothing = std::nullopt) {
    UnsyncedRandom r(1);
    BattleCamShot s = static_shot(object({10.0f, 10.0f, 10.0f}), false, r);
    s.target = id;
    s.length = length;
    s.smoothing = smoothing;
    return s;
}

void test_interesting_chain() {
    World w;
    BattleCamGates g;
    BattleCam cam;
    // No clash: Movie Mode starts the interesting-object shot on the selection (first = true).
    w.interesting = {timed(1, 60, 0.9f), timed(2, 30), timed(3, 30)};
    CHECK(cam.movie_key_up(g, w.picks()) && cam.state() == State::Movie && cam.shot().target == 1);
    CHECK(w.interesting_first.size() == 1 && w.interesting_first[0]);
    CHECK(cam.terrain_clamp());  // 0x4a53f0 step 2: every generated shot sets the sticky land clamp
    CHECK(cam.tick(30, false, w.picks()) == 30 && cam.shot().target == 1);
    // Expiry: held key or Tab freezes the last frame.
    CHECK(cam.key_down(Key::MovieMode, g, w.picks()) && cam.tick(60, false, w.picks()) == 30 && cam.shot().target == 1);
    CHECK(!cam.movie_key_up(g, BattleCamPicks{}) && cam.shot().target == 1);  // released, nothing new to pick: runs on
    CHECK(cam.tick(61, true, w.picks()) == 30 && cam.shot().target == 1);
    // Released: the chain picks the next object (first = false); the smoothing carries over (unset on the new shot).
    const uint32_t serial = cam.serial();
    CHECK(cam.tick(61, false, w.picks()) == 0 && cam.shot().target == 2 && cam.serial() == serial + 1);
    CHECK(w.interesting_first.size() == 2 && !w.interesting_first[1]);
    CHECK(near(cam.smooth_eye({0.0f, 0.0f, 0.0f}, 0), {0.0f, 0.0f, 0.0f}) && near(cam.smooth_eye({100.0f, 0.0f, 0.0f}, 1), {10.0f, 0.0f, 0.0f}));
    // A key up in state 3 also moves to the next interesting object.
    CHECK(cam.movie_key_up(g, w.picks()) && cam.shot().target == 3);
    // A clash comes first in the chain.
    w.clashes = {4};
    CHECK(cam.tick(30, false, w.picks()) == 0 && cam.shot().clash == 4 && !cam.shot().interesting);
    CHECK(cam.tick(5000, false, w.picks()) == 5000 && cam.active());  // the clash shot has no length
    // The clash ends: the run is still on an interesting object (+0x360 outlives the clash shot), so it picks another.
    w.clashes.clear();
    w.interesting = {timed(7, 30)};
    cam.clash_ended(w.picks());
    CHECK(cam.active() && cam.shot().target == 7 && cam.shot().interesting);
    // Nothing left: the expiry stops the cam (End_Cinematic_Mode), which resets the smoothing.
    w.clashes.clear();
    w.interesting = {timed(5, 10)};
    cam.stop();
    CHECK(cam.movie_key_up(g, w.picks()) && cam.shot().target == 5);
    CHECK(cam.tick(10, false, w.picks()) == 0 && !cam.active());
    // b_camera_t starts it too (first = true).
    w.interesting = {timed(6, 10)};
    cam.toggle(g, w.picks());
    CHECK(cam.active() && cam.shot().target == 6 && w.interesting_first.back());
}

void test_toggle_and_smoothing() {
    World w;
    BattleCamGates g;
    BattleCam cam;
    cam.toggle(g, w.picks());  // no clash: nothing
    CHECK(!cam.active());
    w.clashes = {3};
    cam.toggle(g, w.picks());
    CHECK(cam.state() == State::Movie && cam.shot().clash == 3);
    cam.toggle(g, w.picks());  // a running cam stops (never cycles)
    CHECK(!cam.active());
    // b_camera_t skips the key-up gates.
    BattleCamGates locked = g;
    locked.controls_locked = true;
    locked.combat_cinematic = false;
    cam.toggle(locked, w.picks());
    CHECK(cam.active());
    cam.stop();
    // Smoothing 0.9 per shot frame: clock 0 is the keyed eye, then 10 % of the way per frame.
    CHECK(cam.key_down(Key::CinematicView, g, w.picks()));
    CHECK(near(cam.smooth_eye({0.0f, 0.0f, 0.0f}, 0), {0.0f, 0.0f, 0.0f}));
    CHECK(near(cam.smooth_eye({100.0f, 0.0f, 0.0f}, 1), {10.0f, 0.0f, 0.0f}));
    CHECK(near(cam.smooth_eye({100.0f, 0.0f, 0.0f}, 1), {10.0f, 0.0f, 0.0f}));  // the same clock: unchanged
    CHECK(near(cam.smooth_eye({100.0f, 0.0f, 0.0f}, 3), {27.1f, 0.0f, 0.0f}));  // two frames: 1 - 0.9^2 of the rest
    // The hero clash shot has no smoothing.
    cam.stop();
    cam.toggle(g, w.picks());
    CHECK(near(cam.smooth_eye({0.0f, 0.0f, 0.0f}, 0), {0.0f, 0.0f, 0.0f}));
    CHECK(near(cam.smooth_eye({100.0f, 0.0f, 0.0f}, 1), {100.0f, 0.0f, 0.0f}));
}

}  // namespace

int main() {
    test_orbit();
    test_views();
    test_movie_mode();
    test_mouse();
    test_cinematic_view();
    test_polish();
    test_saliency_rank();
    test_interesting_pick();
    test_interesting_templates();
    test_ground_templates();
    test_ground_dispatch();
    test_interesting_chain();
    test_toggle_and_smoothing();
    std::printf(g_fail == 0 ? "game battle cam tests passed\n" : "%d game battle cam check(s) FAILED\n", g_fail);
    return g_fail == 0 ? 0 : 1;
}
