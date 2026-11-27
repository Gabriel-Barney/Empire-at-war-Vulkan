// The tactical battle-cam controller (#462, docs/re/BATTLE_CAM.md): Movie Mode (hotkey slot 78, on key up: 0x2cc080),
// Cinematic View (slot 79, key down: 0x2cbd00), the b_camera_t button's toggle (0x2cca80) and the mouse rules while a
// shot runs (0x2cc3e0). In retail these live in the CinematicClass at 0x140b27a10, the same controller as the TSW firing
// shot (battle_fx.hpp).
//
//   BattleCam             the state machine: 0 none, 2 Cinematic View, 3 battle cam. The host supplies the gates and
//                         the shots it can frame now (BattleCamPicks), and applies the camera, letterbox and HUD.
//   cinematic_view_shot   0x4a6790: one of 8 views round the local player's first selected object.
//   hero_clash_shot       0x4a4ac0: the local player's hero in a land hero clash, held 45 frames, then an orbit.
//
// Presentation only. Nothing here is written into the sim, and the orbit direction is an unsynced draw.
// Ported shots: Cinematic View, the hero clash orbit, and the interesting-object shot with its orbit, fly-past and
// static templates (#568), its push-in, drive-by and land flying shots (#569), and its structure, attack-run and
// fighter shots (#570), the hardpoint shot with its FOV zoom (#571), and the land bombing run (0x49f370, #563). [port]
// The glory files (#572) are not ported yet: that step fails, and the dispatcher goes on, as retail does on a failure.
#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

#include "alamo/game/attract_mode.hpp"
#include "alamo/game/battle_fx.hpp"

namespace alamo::game {

// #571 the FOV zoom trigger (ctrl +0x328 / +0x324 / +0x320) a hardpoint shot sets: `mode` 0 none, 1 a quick zoom, 2 a
// two-step; `target` the object to fit (0 = none); `earliest` the shot frame it may fire after.
struct ZoomTrigger {
    int32_t mode = 0;
    uint32_t target = 0;
    int32_t earliest = 0;
};

// #571 a FOV key (list ctrl +0x30, 0x14-byte records): from `frame` on, degrees; mode 1 (linear with eases) here.
struct FovKey {
    int32_t frame = 0;
    float degrees = 0.0f;
    bool hold = false;  // mode 2: the value holds to the next key
    float ease_out = 0.0f, ease_in = 0.0f;
};

// #571 0x49bdf0 [H] (docs/re/BATTLE_CAM.md "The FOV zoom"): the battle cam's FOV keys and zoom trigger. With keys, the
// value is F0 before the first key, the last key's from it on, and between keys a and b a.degrees + accel(t,
// a.ease_out, b.ease_in) (b - a) (a hold keeps a). With no keys, an armed trigger fires when the shot frame is past
// `earliest`, 36 or more frames are left and the target resolves, and fit = 2 atan(R / D) (R = the target's |E|, D =
// |look - eye|) is more than 10 degrees from F0: T = clamp(fit, 25, 55); mode 1 keys (p + 1, F0), (p + 7, T); mode 2
// (p + 1, F0), (p + 7, mid), (p + 22, mid), (p + 28, T) with mid = F0 + 0.75 (T - F0); eases 0.1 / 0.1. The trigger
// is then cleared; a fit within 10 degrees leaves it armed, so it is re-tried each frame.
class ShotFov {
public:
    static constexpr int32_t kMinFramesLeft = 36;
    static constexpr float kFitTolerance = 10.0f, kMinFov = 25.0f, kMaxFov = 55.0f, kMidShare = 0.75f, kEase = 0.1f;
    void arm(const ZoomTrigger& t) { trigger_ = t; }  // the hardpoint template (mode 0 disarms)
    void clear_keys() { keys_.clear(); }              // 0x4a7a90, every shot start; the trigger is kept
    void clear() { keys_.clear(); trigger_ = {}; }    // End_Cinematic_Mode 0x49b5c0
    uint32_t target() const { return trigger_.mode != 0 ? trigger_.target : 0; }  // +0x324 while armed
    const std::vector<FovKey>& keys() const { return keys_; }
    // This frame's FOV in degrees. `remaining` = the shot timer's frames left (0x4ad2e0); `target_radius` = the trigger
    // target's |E| (nullopt: it no longer resolves); `distance` = |look - eye| of this frame, before the smoothing.
    float value(int32_t frame, int32_t remaining, float base_deg, std::optional<float> target_radius, float distance);

private:
    ZoomTrigger trigger_;
    std::vector<FovKey> keys_;
};

// A battle-cam shot: eye and look keys attached to one object; shot clock 0 = the frame it started.
struct BattleCamShot {
    uint32_t target = 0;             // the framed object (the keys' +0x2c)
    uint64_t clash = 0;              // the hero clash it frames (+0x358), 0 = none
    std::vector<ShotKey> eye, look;
    // +0x270: eye = f x previous eye + (1 - f) x keyed eye, once per shot frame. Unset (#568): the previous shot's value
    // carries on (chained shots never reset it; stopping the cam does).
    std::optional<float> smoothing;
    uint32_t second = 0;             // #570: the object the keys with `on_second` sit on (the target), 0 = none
    bool handheld = false;           // #564: 0x4ab780 starts the handheld noise (handheld_shake) with the shot
    int32_t handheld_frames = kHandheldFrames;  // its duration
    int32_t length = 0;              // #568: frames (0x4ad3a0); 0 = no length (it runs until input or the next key up)
    bool interesting = false;        // #568: an interesting-object shot; starting one sets +0x360, so the chain reaches the next
    bool bombing_run = false;        // #563 (+0x32c): the shot ends with the local bombing run (bombing_run_ended)
    bool engine_loop = true;         // the shot moves the cinematic engine-loop SFX (+0x318) to its object (#563: not the run)
    // #671 +0x1bd: true = the view looks at the filtered look (LookFilter), false = the raw look. Unset: the template
    // does not write it, so the previous shot's value carries on (End_Cinematic_Mode clears it).
    std::optional<bool> look_filter;
    int32_t hardpoint = -1;  // #571: the hardpoint of `target` the keys with `on_hardpoint` sit on (-1 = none)
    std::optional<ZoomTrigger> zoom;  // #571: the zoom trigger the shot sets at its start (unset: left as it was)
};

// #671 0x4a96d0 [H] (seeded by 0x49ff50 when a shot's first look key goes in): a lag-compensated moving average of the
// look point. N = FPS + 1 samples of history H, averages S, velocities V and accelerations Acc; weights w[i] = i / N + 1
// with C their running sums; A = (int)(FPS x 0.5), L = (int)(FPS x 0.7). Per new frame: avg = (look + H[0..A-2]) / A,
// then H, S, V, Acc each drop their last and take look, avg, S[0] - S[1], V[0] - V[1] in front, and the output is
// S[L] + (sum_{i<L} w[i] V[i]) / C[L-1] x (0.25 + 0.7) x FPS + (sum_{i<L} w[i] Acc[i]) / C[L-1]. A frame at or before
// the last one updates nothing. The state updates even while the raw look is asked for. Unseeded: the input.
class LookFilter {
public:
    static constexpr float kLagSeconds = 0.7f;      // ctrl +0x1d0
    static constexpr float kAverageSeconds = 0.5f;  // ctrl +0x1d4
    void seed(const tactical::Vec3& p, int32_t fps = kCinematicFps);  // 0x49ff50
    tactical::Vec3 step(int32_t frame, const tactical::Vec3& look, bool raw);  // 0x4a96d0
    void clear() { on_ = false; }  // End_Cinematic_Mode (+0x1bc)

private:
    bool on_ = false;  // +0x1bc
    int32_t fps_ = kCinematicFps;
    int32_t last_ = 0;  // +0x1c0
    tactical::Vec3 out_;  // +0x1c4
    std::vector<tactical::Vec3> h_, s_, v_, a_;
    std::vector<float> w_, c_;
};

// 0x4a6790 [H]: view n (0..7) steps 45 degrees clockwise round the object: yaw 0, 315, 270 ... 45.
float cinematic_view_yaw(int32_t view);
// 0x4a6790 [H]: pitch 30 degrees in space (mode type 2), 45 on land.
float cinematic_view_pitch(bool space);
// 0x4a6790 [H]: d = clamp(3 x avg(E) x zoom, 3|E|, 10|E|), E = the half-extents; zoom = +0x1a8 [port: 1, the wheel
// zoom 0x4ac520 is not read].
float cinematic_view_distance(const tactical::Vec3& half_extents, float zoom = 1.0f);
// The eye offset in the object's frame: yaw about the object's Z from its +Y side, pitch up from the horizontal. [M] the
// exact axes of 0x4a6790's Rx(90) Rz(yaw) Rx(pitch) product and the side view 0 sits on.
tactical::Vec3 cinematic_view_offset(float yaw_deg, float pitch_deg, float distance);
// 0x4a6790 [H]: one attached linear eye key at the view offset and one look key on the object, eye smoothing 0.9, no length.
BattleCamShot cinematic_view_shot(uint32_t target, const tactical::Vec3& half_extents, float yaw_deg, float pitch_deg,
                                  float zoom = 1.0f);

// #564 (0x49cd00, +0x315 [H]): the land terrain clamp of the eye: at least 15 over the highest ground of a 3 x 3 grid at
// +-15 around it. `ground` = the map's height at (x, y).
tactical::Vec3 terrain_clamped_eye(const tactical::Vec3& eye, const std::function<float(float x, float y)>& ground);

// 0x4a4ac0 [H]: R = 7.5 x avg(E), clamped to 125..500 (mode type 2: 250..1000).
float hero_clash_radius(const tactical::Vec3& half_extents, bool space);
// 0x4a4ac0 [H]: eye held at (0, R, 0.67R) in the hero's frame for 45 frames, then an orbit of the hero (one turn per 600
// frames, `reverse` = clockwise); the look holds on the hero. No length: it runs until input, the next key up or the
// clash's end.
BattleCamShot hero_clash_shot(uint32_t hero, uint64_t clash, const tactical::Vec3& half_extents, bool space, bool reverse);

// #563 0x49f370 [H]: the land bombing run, Movie Mode's pick after the hero clash. Needs the local player's run in
// progress (mode +0x38, 0x494480). The bomber is the record's first object (+0x30), the target its first marker (+0x48).
struct BombingRunView {
    uint32_t bomber = 0, target = 0;
    tactical::Vec3 bomber_at, target_at;  // world positions now
    float speed = 0.0f;                   // the bomber's |velocity| per frame
    float range = 0.0f;                   // its attack range (behaviour 9 vf+0x140)
};
// n = (int)((|target - bomber| - range) / speed), the frames until the bomber is in range. Eye: linear keys on the
// bomber at (15, 60, 30), frames 0 and n - 30, then an orbit of the target at (300, 300, 250) from frame n, period 600,
// clockwise. Look: one linear world key at the target's position, frame n - 30. Length (int)(FPS x (600 / FPS +
// (int)(dist / speed))) [M: 0x4ad3a0 takes seconds, so the 600 meant as frames is 600 frames and the travel time is
// FPS x frames: a retail seconds / frames mix-up, kept]. Smoothing 0, no handheld noise, the raw look; it ends with the
// run (bombing_run_ended); +0x318 (the engine-loop SFX) is left as it was. One unsynced rand(0, 1) is drawn first (both
// outcomes take the same branch). [port] A bomber with no speed fails (retail divides by it).
bool bombing_run_shot(const BombingRunView& v, UnsyncedRandom& rng, BattleCamShot& out);

// What the host reads each time a key or button reaches the controller.
struct BattleCamGates {
    bool in_game_cinematics = true;  // GameConstants In_Game_Cinematics (DAT_140b16dd2)
    bool combat_cinematic = true;    // the story reward ENABLE_COMBAT_CINEMATIC (DAT_140b305ec, default on)
    bool controls_locked = false;    // LOCK_CONTROLS (DAT_140b30392) [M]
    bool space = false;              // mode type 2
};

// The shots the host can frame now; each returns false when it has nothing to frame.
struct BattleCamPicks {
    // 0x4a6790's object: `target` 0 = the local player's first selected object (a new view), else that object (the
    // mouse or the wheel turning the running view, 0x4ac830 / 0x4ac520). Out: its id and half-extents E.
    std::function<bool(uint32_t target, uint32_t& id, tactical::Vec3& half_extents)> view;
    // 0x4a4ac0 via the clash list (land mode +0x3f8): the clash after `after` in list order, wrapping and never `after`
    // itself (`after` 0 or gone: the first clash), framing its first participant of the local player.
    std::function<bool(uint64_t after, BattleCamShot& out)> clash;
    // #568 0x2cd930(ctrl, first): the interesting-object pick and its template (interesting_pick, interesting_shot).
    // `first` = a random object of the selection; otherwise the next object of the chain.
    std::function<bool(bool first, BattleCamShot& out)> interesting;
    // #563 0x49f370: the land bombing run of the local player (bombing_run_shot), tried after the clash.
    std::function<bool(BattleCamShot& out)> bombing_run;
};

class BattleCam {
public:
    enum class State : uint8_t { None = 0, View = 2, Movie = 3 };  // +0x37c (4 = a script cinematic: not here)
    // A key as 0x2cbd00 sees it: its hotkey slot first (0x2127f0), then the raw key code.
    enum class Key : uint8_t { MovieMode, CinematicView, Tether, Escape, Control, Other };

    State state() const { return state_; }
    bool active() const { return state_ != State::None; }
    int32_t view() const { return view_; }  // +0x354
    float view_yaw() const { return yaw_; }
    float view_pitch() const { return pitch_; }
    bool key_held() const { return held_; }  // +0x317
    const BattleCamShot& shot() const { return shot_; }
    // Bumped by every shot start (a new view, the next clash): the host restarts the shot clock and the smoothing.
    uint32_t serial() const { return serial_; }

    // 0x2cbd00, a key pressed (no GUI dialog open, a tactical battle). True = the key is swallowed.
    bool key_down(Key k, const BattleCamGates& g, const BattleCamPicks& p);
    // 0x2cc080, the Movie Mode key released. True = swallowed.
    bool movie_key_up(const BattleCamGates& g, const BattleCamPicks& p);
    // 0x2cca80(ctrl, 0), b_camera_t when the camera is already at its default pitch and yaw: a running cam stops,
    // otherwise Movie Mode's first pick starts (without the key-up gates).
    void toggle(const BattleCamGates& g, const BattleCamPicks& p);
    // 0x2cc3e0, one frame of mouse input. Battle cam: a move of 1/20 of the screen or more since the previous position,
    // or any button / wheel event, stops it. Cinematic View: the move's dominant axis / 4 turns the view (pitch +-80,
    // yaw wrapped), buttons do nothing, and the wheel (`wheel` notches, + = away) zooms (0x4ac520). The cursor position
    // is the baseline either way.
    void mouse(int32_t x, int32_t y, uint32_t width, uint32_t height, bool button, float wheel, const BattleCamPicks& p);
    // #564 (0x4ac520 [H]): the Cinematic View zoom factor +0x1a8. 1.0 at the start and never reset: each wheel notch moves
    // the view 25 units (away = closer) from 3 avg(E) x zoom, clamped to [3|E|, 10|E|], and the factor keeps the ratio.
    float zoom() const { return zoom_; }
    // #564 (+0x315 [H]): the land terrain clamp of the eye. Sticky: Cinematic View sets it, nothing here clears it.
    bool terrain_clamp() const { return terrain_clamp_; }
    // 0x2cbc40: the framed clash ended. The chain (0x2cda20), else stop.
    void clash_ended(const BattleCamPicks& p);
    // #563 0x4aaf00 (+0x32c): the framed bombing run is no longer in progress. The chain, else stop. A run shot whose
    // length expired (frozen while the key or Tab is held) no longer ends with the run: the expiry clears +0x32c.
    void bombing_run_ended(const BattleCamPicks& p);
    // #568 0x4aaf00, once per shot frame: `clock` = frames since the shot started. A shot with a length expires at it:
    // outside the battle cam it ends; while the Movie Mode key or Tab is held the clock freezes on its last value;
    // otherwise the chain (0x2cda20: the next clash, then another interesting object if this one is) or stop. Returns
    // the clock to draw (0 after a chained start).
    int32_t tick(int32_t clock, bool tab_held, const BattleCamPicks& p);
    // 0x2cbb80 (and End_Cinematic_Mode 0x49b5c0).
    void stop();

    // +0x270 eye smoothing at shot clock `clock` (once per clock value; the first frame is the keyed eye).
    tactical::Vec3 smooth_eye(const tactical::Vec3& keyed, int32_t clock);
    // #671 the view's look at shot clock `clock` (0x49cd00 -> 0x4a96d0): the filter is seeded with the first look of each
    // shot [port: retail seeds it with look key 0 resolved as the key goes in, the same frame] and steps every frame;
    // with +0x1bd clear the raw look is drawn.
    tactical::Vec3 filter_look(const tactical::Vec3& look, int32_t clock);
    bool look_filter() const { return look_filter_; }  // +0x1bd
    // #571 0x49bdf0: the FOV keys and the zoom trigger. A shot start clears the keys and arms the shot's trigger, if it
    // has one; stop() clears both.
    const ShotFov& fov() const { return fov_; }
    // This shot frame's FOV in degrees (ShotFov::value with the frames left of a shot with a length, else 0).
    float shot_fov(int32_t clock, float base_deg, std::optional<float> target_radius, float distance);

private:
    void start(State s, BattleCamShot shot);
    bool chain(const BattleCamPicks& p);  // 0x2cda20
    bool open_view(const BattleCamGates& g, const BattleCamPicks& p);
    bool rebuild_view_eye(const BattleCamPicks& p);  // 0x4ac830: the running view's eye key at yaw / pitch / zoom

    State state_ = State::None;
    int32_t view_ = 0;
    float yaw_ = 0.0f, pitch_ = 0.0f;  // +0x1b4 / +0x1b8
    bool held_ = false;
    bool has_cursor_ = false;
    int32_t cursor_x_ = 0, cursor_y_ = 0;  // +0x398 / +0x39c
    BattleCamShot shot_;
    tactical::Vec3 view_extents_;  // the viewed object's E (+0x1a4 = 3 avg(E))
    float zoom_ = 1.0f;            // +0x1a8
    bool terrain_clamp_ = false;   // +0x315
    uint32_t serial_ = 0;
    int32_t smooth_clock_ = -1;
    float smoothing_ = 0.0f;  // +0x270, kept across chained shots
    bool look_filter_ = false;  // +0x1bd, kept across chained shots (#671)
    LookFilter filter_;
    uint32_t filter_serial_ = 0;  // the shot the filter was seeded for
    ShotFov fov_;  // #571
    bool on_interesting_ = false;  // +0x360 set: the run reached an interesting object (a chained clash keeps it)
    int32_t last_clock_ = 0;
    tactical::Vec3 smooth_eye_;
};


// ---- #568 the interesting-object shot (0x2cd930 / 0x2cd0d0 / 0x520df0 / 0x4a53f0; docs/re/BATTLE_CAM.md) -------------

// 0x520df0 [H]: the candidates' order, most salient first. Per column: size, power, targets and speed as
// (v - min) / (max - min); x and y as 1 - |v - mean| / (max - min); health as 1 - (v - min) / (max - min) (a zero range
// divides by 1). Each normalised column is then scaled by its own spread (max - min), and the score is the weighted sum
// (GameConstants Saliency_*). [port] Equal scores keep the candidates' order (retail's introsort is not stable).
std::vector<size_t> saliency_rank(std::span<const SaliencyFeatures> features, const SaliencyWeights& w);

// The pick's memory (+0x360 / +0x368 / +0x370): the current object, the id the validation stored, the last type.
struct InterestingState {
    uint32_t current = 0;  // +0x360
    uint32_t stored = 0;   // +0x368: written only by the validation step (the retail bug, BATTLE_CAM.md "The pick")
    int32_t type = -1;     // +0x370
    int32_t hardpoint = -1;  // #571 +0x378: the hardpoint the ranked pick drew (-1 = none; every pick resets it)
};

// What the pick reads from the battle.
struct InterestingWorld {
    std::vector<uint32_t> selection;                  // the local player's selection, list order
    std::function<int32_t(uint32_t id)> type_of;      // the object's type, -1 = it no longer resolves (0x2aca60)
    // The current object may lead to its target ((obj +0x3a0 & 0x40) == 0 [L]).
    std::function<bool(uint32_t id)> may_follow;
    // The object's current target when it passes the branch's filters (in the world, not fogged, not hidden, not a
    // TEAM or TRANSPORT container, a model), else 0.
    std::function<uint32_t(uint32_t id)> target;
    std::vector<uint32_t> candidate_ids;              // the filtered candidates (0x2cd0d0 step C)
    std::vector<int32_t> candidate_types;
    std::vector<SaliencyFeatures> candidate_features;
    SaliencyWeights weights;
    // #571: the object's hardpoint count (obj +0x2d0), and whether hardpoint `hp` is live (not a destroyed
    // Is_Destroyable one) with a target (0x3856a0). Unset: no hardpoints.
    std::function<uint32_t(uint32_t id)> hardpoint_count;
    std::function<bool(uint32_t id, uint32_t hp)> hardpoint_ready;
};

// 0x2cd930 / 0x2cd0d0 [H]: `first` = a random object of the selection (no filter); otherwise validate the current
// object (step A), follow its target with chance 2/3 (step B), else one of the first min(n - 1, 10) + 1 ranked
// candidates that differs from the current object and the last type (k tries, else the first). False = nothing.
// #571: after a ranked pick only, with chance 3/4 (rand(0, 3) > 0), up to n draws of rand(0, n - 1) over the object's
// n hardpoints, with replacement, keep the first ready one in `hardpoint`.
bool interesting_pick(InterestingState& s, bool first, const InterestingWorld& w, UnsyncedRandom& rng);

// One object as the dispatcher and the templates see it.
// #570: the object's current target (0x397a40(obj, 0)), as the attack run and the structure shot read it.
struct InterestingTarget {
    uint32_t id = 0;                // 0: no target
    bool model = true;              // a model (+0x2a0)
    tactical::Vec3 half_extents{1.0f, 1.0f, 1.0f};
    ObjectFrame frame;              // its transform at the start (position +0x78, yaw +0x8c from axis 0)
};
struct InterestingObject {
    uint32_t id = 0;
    uint64_t category = 0;          // CategoryMask (type +0x1648)
    bool locomotor = false;         // a locomotor behaviour (obj +0x333 != -1)
    bool flying = false;            // FLYING_LOCOMOTOR
    bool model = true;              // a model (+0x2a0)
    tactical::Vec3 half_extents{1.0f, 1.0f, 1.0f};  // E (ObjBox / ModelBox [M: one box in the port])
    ObjectFrame frame;              // its transform at the start (M)
    std::string_view type_name;     // type +0xf8
    InterestingTarget combat_target;  // #570: what it is shooting at (not BattleCamShot::target, the framed object)
    // #571: the pick's hardpoint (InterestingState::hardpoint), its target now (0x3856a0; 0 = none), and whether that
    // target passes the template's gates: not fogged or hidden for the local player, faded in to 0.75 or more.
    int32_t hardpoint = -1;
    uint32_t hardpoint_target = 0;
    bool hardpoint_target_shown = false;
};
// GameObjectCategoryType values the dispatcher tests (the enum file's; [H] defaults).
struct InterestingCategories {
    uint64_t fighter = 0x1, bomber = 0x2, corvette = 0x8, frigate = 0x10, infantry = 0x80, vehicle = 0x100;
};

// #570 0x49f880 [H]: no locomotor (a structure, a turret). The eye pulls back over n = 60..90 frames from
// (sx Ex, sy Ey, 2 Ez) to (2 sx Ex, 2 sy Ey, 3 Ez); with a target the look then turns to it and holds, so the length is
// 2n, else n. Never fails.
BattleCamShot structure_shot(const InterestingObject& o, UnsyncedRandom& rng);
// #570 0x4a26f0 [H]: the attack run. It needs a target with a model, inside a 30-degree cone off the nose and heading
// more than 80 degrees apart; then the eye rides the hull for T = 90..120 frames and swings onto the target by 2T,
// where the look ends. Every draw is made even when it fails. Retail's double degree-to-radian conversion is kept.
bool attack_run_shot(const InterestingObject& o, UnsyncedRandom& rng, BattleCamShot& out);
// #570 0x4a0140 [H]: the fighter chase, from a side within 45 degrees of the nose or the tail; smoothing 0.99, handheld
// noise. [port] Retail's CameraAnimeSpiral wander over the shot is left out (its parameter slots are [M]).
BattleCamShot fighter_chase_shot(const InterestingObject& o, UnsyncedRandom& rng);
// #569 0x4a4e30 [H] (needs a model): the infantry push-in. n = 120..150, h = 1.5..2.0; the eye moves on world points
// M (2s Ex, 2 Ey, 2h Ez) -> M (s Ex, 2 Ey, h Ez) fixed at the start, the look on the object; length n, handheld noise.
bool push_in_shot(const InterestingObject& o, UnsyncedRandom& rng, BattleCamShot& out);
// #569 0x4a6dd0 [H] (needs a model): the vehicle drive-by, one of three variants; every eye key a world point fixed at the
// start, so the vehicle drives through the frame; length 2n, n = 60..90; no noise.
bool drive_by_shot(const InterestingObject& o, UnsyncedRandom& rng, BattleCamShot& out);
// #569 0x4a23d0 [H] (needs a model): a land flyer seen from a fixed world point M (s Ex, -Ey, Ez) beside it, the look
// held on it; length n = 60..90; no noise.
bool land_flying_shot(const InterestingObject& o, UnsyncedRandom& rng, BattleCamShot& out);

// 0x4a17c0 [H]: a tripod at C + d (cos phi cos psi, cos phi sin psi, sin phi), psi = 0..360, phi = 20..45 (space:
// negated half the time), d = 2|E|..4|E|; eye and look held on world points; 60 frames, handheld noise.
BattleCamShot static_shot(const InterestingObject& o, bool space, UnsyncedRandom& rng);
// 0x4a0830 [H]: an orbit of the object at radius 2|E| (either side), height 1.25 Ez, translate-only keys, one turn per
// 600 frames (ease out 0.25), either direction; length 90 + 180 t frames (t = smoothstep of (|E| - 30) / 470); 0.9.
BattleCamShot orbit_shot(const InterestingObject& o, UnsyncedRandom& rng);
// 0x4a1c90 [H]: variant A (space, half the time, not Nebulon_B_Frigate / Alliance_Assault_Frigate) slides under the
// hull for (t + 1) x 8 s; variant B sweeps across the top front along the start's axes for 3 + 4t s; smoothing 0.9.
// Needs a model.
bool fly_past_shot(const InterestingObject& o, bool space, UnsyncedRandom& rng, BattleCamShot& out);
// #571 0x49d640 (a key with a hardpoint) [H]: the hardpoint frame. `attach` = the Attachment_Bone's world translation
// (Fire_Bone_A's without one), `fire_x` = Fire_Bone_A's world X axis d. The axes are M Rz(90), M = Rz(yaw) Ry(pitch)
// with yaw = atan2(d.y, d.x) and pitch = -atan2(d.z, |d.xy|) (M maps +X to d), so an offset (0, 0, h) sits at
// attach + h (-sin(e) cos(yaw), -sin(e) sin(yaw), cos(e)), e = the barrel's elevation.
ObjectFrame hardpoint_eye_frame(const tactical::Vec3& attach, const tactical::Vec3& fire_x);
// #571 0x4a47d0 [H]: needs the pick's hardpoint and its target, shown. n = 120..180 is drawn first, then z = 0..2. The
// eye holds 200 above the hardpoint (on_hardpoint), the look on the target (on_second, + its Ranged_Target_Z_Adjust);
// length (int)(FPS x n / FPS), handheld noise 2n, smoothing 0.9, no letterbox call, +0x1bd inherited. z arms the FOV
// zoom on the target: none, after n / 2, or the two-step after n / 2 - 5. The gates fail before any draw.
bool hardpoint_shot(const InterestingObject& o, UnsyncedRandom& rng, BattleCamShot& out);
// 0x4a53f0 [H]: the templates in retail's order by category, with retail's draws: step 3 is the hardpoint shot (#571).
// [port] Step 1, the glory files (#572), is not ported: it fails, and the dispatcher falls through to the next test,
// as retail does when a template fails.
bool interesting_shot(const InterestingObject& o, bool space, const InterestingCategories& c, UnsyncedRandom& rng,
                      BattleCamShot& out);

}  // namespace alamo::game
