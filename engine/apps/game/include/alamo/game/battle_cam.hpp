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
// Ported shots: Cinematic View and the hero clash orbit. [port] Retail's other Movie Mode picks are not ported yet (the
// land bombing run 0x49f370, the interesting-object shot 0x2cd930 -> 0x4a53f0), so with no hero clash Movie Mode
// frames nothing, as retail does when every pick fails.
#pragma once

#include <cstdint>
#include <functional>
#include <vector>

#include "alamo/game/battle_fx.hpp"

namespace alamo::game {

// A battle-cam shot: eye and look keys attached to one object; shot clock 0 = the frame it started.
struct BattleCamShot {
    uint32_t target = 0;             // the framed object (the keys' +0x2c)
    uint64_t clash = 0;              // the hero clash it frames (+0x358), 0 = none
    std::vector<ShotKey> eye, look;
    float smoothing = 0.0f;          // +0x270: eye = f x previous eye + (1 - f) x keyed eye, once per shot frame
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
    // 0x4a6790 at this yaw / pitch: `target` 0 = the local player's first selected object (a new view), else that
    // object (the mouse turning the running view, 0x4ac830).
    std::function<bool(uint32_t target, float yaw_deg, float pitch_deg, BattleCamShot& out)> view;
    // 0x4a4ac0 via the clash list (land mode +0x3f8): the clash after `after` in list order, wrapping and never `after`
    // itself (`after` 0 or gone: the first clash), framing its first participant of the local player.
    std::function<bool(uint64_t after, BattleCamShot& out)> clash;
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
    // yaw wrapped). The cursor position is the baseline either way.
    void mouse(int32_t x, int32_t y, uint32_t width, uint32_t height, bool button_or_wheel, const BattleCamPicks& p);
    // 0x2cbc40: the framed clash ended. The next clash, else stop.
    void clash_ended(const BattleCamPicks& p);
    // 0x2cbb80 (and End_Cinematic_Mode 0x49b5c0).
    void stop();

    // +0x270 eye smoothing at shot clock `clock` (once per clock value; the first frame is the keyed eye).
    tactical::Vec3 smooth_eye(const tactical::Vec3& keyed, int32_t clock);

private:
    void start(State s, BattleCamShot shot);
    bool open_view(const BattleCamGates& g, const BattleCamPicks& p);

    State state_ = State::None;
    int32_t view_ = 0;
    float yaw_ = 0.0f, pitch_ = 0.0f;  // +0x1b4 / +0x1b8
    bool held_ = false;
    bool has_cursor_ = false;
    int32_t cursor_x_ = 0, cursor_y_ = 0;  // +0x398 / +0x39c
    BattleCamShot shot_;
    uint32_t serial_ = 0;
    int32_t smooth_clock_ = -1;
    tactical::Vec3 smooth_eye_;
};

}  // namespace alamo::game
