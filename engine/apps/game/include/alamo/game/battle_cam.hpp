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
// Ported shots: Cinematic View, the hero clash orbit, and (#568) the interesting-object shot with its orbit, fly-past and
// static templates. [port] The land bombing run (0x49f370, #563) and the other interesting-object templates (#569 -
// #572) are not ported yet: they fail, and the next pick is tried, as retail does when a pick fails.
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

// A battle-cam shot: eye and look keys attached to one object; shot clock 0 = the frame it started.
struct BattleCamShot {
    uint32_t target = 0;             // the framed object (the keys' +0x2c)
    uint64_t clash = 0;              // the hero clash it frames (+0x358), 0 = none
    std::vector<ShotKey> eye, look;
    // +0x270: eye = f x previous eye + (1 - f) x keyed eye, once per shot frame. Unset (#568): the previous shot's value
    // carries on (chained shots never reset it; stopping the cam does).
    std::optional<float> smoothing;
    bool handheld = false;           // #564: 0x4ab780 starts the handheld noise (handheld_shake) with the shot
    int32_t handheld_frames = kHandheldFrames;  // its duration
    int32_t length = 0;              // #568: frames (0x4ad3a0); 0 = no length (it runs until input or the next key up)
    bool interesting = false;        // #568: an interesting-object shot (+0x360 set): its expiry chains to another one
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
    // #568 0x4aaf00, once per shot frame: `clock` = frames since the shot started. A shot with a length expires at it:
    // outside the battle cam it ends; while the Movie Mode key or Tab is held the clock freezes on its last value;
    // otherwise the chain (0x2cda20: the next clash, then another interesting object if this one is) or stop. Returns
    // the clock to draw (0 after a chained start).
    int32_t tick(int32_t clock, bool tab_held, const BattleCamPicks& p);
    // 0x2cbb80 (and End_Cinematic_Mode 0x49b5c0).
    void stop();

    // +0x270 eye smoothing at shot clock `clock` (once per clock value; the first frame is the keyed eye).
    tactical::Vec3 smooth_eye(const tactical::Vec3& keyed, int32_t clock);

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
};

// 0x2cd930 / 0x2cd0d0 [H]: `first` = a random object of the selection (no filter); otherwise validate the current
// object (step A), follow its target with chance 2/3 (step B), else one of the first min(n - 1, 10) + 1 ranked
// candidates that differs from the current object and the last type (k tries, else the first). False = nothing.
// [port] The hardpoint draw (3/4) is made but no hardpoint is picked: the hardpoint shot is not ported (#571).
bool interesting_pick(InterestingState& s, bool first, const InterestingWorld& w, UnsyncedRandom& rng);

// One object as the dispatcher and the templates see it.
struct InterestingObject {
    uint32_t id = 0;
    uint64_t category = 0;          // CategoryMask (type +0x1648)
    bool locomotor = false;         // a locomotor behaviour (obj +0x333 != -1)
    bool flying = false;            // FLYING_LOCOMOTOR
    bool model = true;              // a model (+0x2a0)
    tactical::Vec3 half_extents{1.0f, 1.0f, 1.0f};  // E (ObjBox / ModelBox [M: one box in the port])
    ObjectFrame frame;              // its transform at the start (M)
    std::string_view type_name;     // type +0xf8
};
// GameObjectCategoryType values the dispatcher tests (the enum file's; [H] defaults). #569 / #570 add the rest.
struct InterestingCategories {
    uint64_t bomber = 0x2, corvette = 0x8;
};

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
// 0x4a53f0 [H]: the templates in retail's order by category, with retail's draws. [port] The templates not ported yet
// (fighter, infantry, attack run, vehicle, structure, flying: #569 / #570; hardpoint #571; glory files #572) fail, so
// the dispatcher falls through to the next test, as retail does when a template fails.
bool interesting_shot(const InterestingObject& o, bool space, const InterestingCategories& c, UnsyncedRandom& rng,
                      BattleCamShot& out);

}  // namespace alamo::game
