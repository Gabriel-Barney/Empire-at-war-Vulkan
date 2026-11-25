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
    CHECK(s.target == 9 && s.clash == 0 && s.eye.size() == 1 && s.look.size() == 1 && near(s.smoothing, 0.9f));
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
    test_toggle_and_smoothing();
    std::printf(g_fail == 0 ? "game battle cam tests passed\n" : "%d game battle cam check(s) FAILED\n", g_fail);
    return g_fail == 0 ? 0 : 1;
}
