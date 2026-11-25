# The tactical battle cam (#462)

The battle cam covers the Movie Mode key (slot 78), the Cinematic View key (slot 79) and the `b_camera_t` button. In
retail they all belong to one `CinematicClass` at `0x140b27a10`, which is also the controller of the TSW firing shot
(legacy `docs/re/TSW_PLANET_DESTROY.md` §11).

The source is the Ghidra decompile of StarWarsG.exe, with call arguments and vtables checked with capstone.
Confidence: [H] read directly from code, [M] inferred, [L] guessed. [port] marks where the port differs.

The port: `engine/apps/game/include/alamo/game/battle_cam.hpp` holds the state machine and the shots, the key
evaluator is in `battle_fx.hpp`, and `engine/apps/game/app/app_cinematic.cpp` is the host.

## The controller

The state is at +0x37c [H]:

| state | meaning |
|---|---|
| 0 | none |
| 2 | Cinematic View |
| 3 | battle cam: Movie Mode, `b_camera_t`, and the story reward 0x6b "Starting Cinematic Mode" via `0x2cd010` |
| 4 | script cinematic: `0x2ccd80`, from `0x44cd80` [M: Lua Start_Cinematic_Camera]. Esc sends `Story_Handle_Esc` |

The other fields [H]:

| field | meaning |
|---|---|
| +0x34a | the cam is running; the per-frame update runs only while it is set. A key handler resets the state to 0 when it is clear |
| +0x348 | story lock, set by `0x2cd010`: both key handlers swallow every key, and the mouse does not end the shot |
| +0x349 | Camera Tether (slot 77) on |
| +0x354 | the Cinematic View index, 0..7 |
| +0x358 | the current hero clash |
| +0x360 / +0x368 / +0x370 / +0x378 | the interesting object, its id, its type and its hardpoint |
| +0x317 | the Movie Mode key is held: an expiring shot is kept |
| +0x315 | the eye's land terrain clamp |
| +0x318 | the object whose cinematic engine-loop SFX plays |
| +0x390..+0x39c | the cursor now, and its baseline |

The game-mode virtuals used [H]:

| vf | Galactic | Land | Space | meaning |
|---|---|---|---|---|
| +0xe0 | 0 | 1 | 2 | mode type |
| +0xf0 | false | true | true | is tactical |
| +0x210 | null | `+0x3f8` (`HeroClashManagerClass`) | null | the hero clash list |

## The gates

- **A GUI dialog is open** (`0x1c3c40(&DAT_140a78510)`, the open-dialog count at +0x98): no handler does anything [H].
- **Key up only:**
  - `DAT_140b305ec` (`0x140b30350 +0x29c`) is the story reward **ENABLE_COMBAT_CINEMATIC**, on by default
    (`0x456970`) [H].
  - `DAT_140b30392` (`+0x42`) is the story reward case 0x19, **LOCK_CONTROLS** [M]. Dialog `0xa2fe0` also forces it
    while it is up.
- **In_Game_Cinematics** (`DAT_140b16dd2`) [H]:
  - Cinematic View needs it to open.
  - The per-frame update `0x2cd870` needs it for every shot.
  - [port] So with it off, no battle-cam shot starts.

## The key evaluator

A key record is 0x54 bytes. `0x499c60` adds an eye key and `0x499dd0` adds a look key; both insert sorted by frame [H].

| off | meaning |
|---|---|
| +0x00 | world position (when +0x2c == -1) |
| +0x18 | offset (attached keys) |
| +0x24 | frame |
| +0x28 | segment mode: 0 spline, 1 linear, 2 hold, 3 orbit |
| +0x2c | the object id; -1 = a world key |
| +0x30 | the hardpoint index; -1 = the object [M] |
| +0x38 | tangent scale (0.5) |
| +0x44 / +0x48 | ease out / ease in |
| +0x4c | orbit period, in frames (literal) |
| +0x50 | orbit reverse (clockwise) |
| +0x51 | translate only (the offset is not rotated by the object) |
| +0x52 | from the muzzle bone `MuzzleA_00` / `MuzzleA_01` |

Position (`0x49d640`) [H]:
- an attached key is the object's model transform × offset, plus the type's `Ranged_Target_Z_Adjust` on z;
- with +0x51 set, it is the model translation + offset.

Eye (`0x49c540`) [H]:
- Between keys a and b: w = accel(σ, a.ease_out, b.ease_in) (`0x49bb40`). The mode of a then picks spline, linear,
  hold, or the polar interpolation `0x49a610`.
- **The last (or only) key, after its frame, with mode 3: the orbit `0x49c810`.**
  ```
  P = resolve(key); L = look(c); d = P - L
  phase = clamp(accel(((c - frame) % period) / period, ease_out, ease_in), 0, 1)
  a = 360 * phase; if reverse: a = 360 - a
  eye = L + Rz(a) * d            (counter-clockwise about world +Z, 0x4800f0)
  ```
  d is re-resolved every frame, so the circle follows the object's facing.
- Otherwise the last key is held.

Look (`0x49de50`): the same, but mode 3 has no look branch and yields (0, 0, 0) [H].

After the eye [H]:
- the land terrain clamp (+0x315): in mode type 1 with the flag set, eye.z = max(eye.z, the highest ground on a 3 × 3
  grid at ±15 around the eye + 15). The flag is sticky: Cinematic View and the interesting-object shot set it, a type's
  cinematic file and a script cinematic clear it, and End_Cinematic_Mode leaves it, so later land shots inherit it;
- the smoothing (+0x270: eye = f × previous eye + (1 − f) × eye);
- the look smoothing `0x4a96d0` [L];
- the shake.

[port] The port evaluates the eye and look with `shot_keys_eye` and `shot_keys_look` (`battle_fx.hpp`):
- They model the hold, linear, spline and orbit-after-the-last-key cases.
- They do not model `0x49a610` (an orbit key followed by another key, which no ported shot uses), the look smoothing,
  the hardpoint / muzzle keys or the roll keys (#562).
- The smoothing is `BattleCam::smooth_eye`, once per shot frame.
- The terrain clamp (#564) is `terrain_clamped_eye` in `battle_cam_camera`, with the flag in `BattleCam::terrain_clamp`.
  The flag is set by Cinematic View. [port] Nothing clears it yet, because the paths that clear it in retail (a type's
  cinematic file, the script cinematic `0x4a5d20`) don't use this controller in the port. The TSW shot doesn't apply
  it, since it only runs in space.

## The handheld noise (`0x4ab780` / `0x4ab060`) [H]

`0x4ab780(ctrl, dur)` clears every shake tracker (+0x278) and starts one CameraShakeEffect over `dur` frames from shot
clock 0. Its parameters are set by the controller constructor `0x499560`, and nothing else writes them [M]:
- XAmplitude 20 and YAmplitude 20;
- Frequency 0.01 × dur;
- EaseIn 0 and EaseOut 0.1;
- NoiseOffset 0 and roll 0.

That is a slow sway, one noise cell per 100 frames and about ±1° of view direction.

The effect's vtable is `0x8fedb8` (apply `0x7d97d0`, the fbm shake). In `0x7d93a0`, Ghidra prints the AnimeSpiral branch
(`0x8fefa0`) first, which is easy to misread.

Who starts it:
- the hero clash and Cinematic View (every view step), with dur 0x3fffffff;
- the interesting-object templates, with the shot length.

The bombing run and the TSW shot don't.

The port: `handheld_shake(dur)` (`battle_fx.hpp`) on `CameraShake`, restarted by the host with each shot that has
`BattleCamShot::handheld`.

## The engine-loop SFX (`0x3a7bb0(obj, on)`) [H]

- **On:** the type's `SFXEvent_Engine_Cinematic_Focus_Loop` (+0x1420) plays attached to the object ("%s: Starting
  cinematic engine loop %s").
- **Off:** it is stopped.
- **Both:** the type's `SFXEvent_Ambient_Moving` (+0x1520) one-shot plays.
- **Gates:** nothing plays for a dying object, and a squadron member uses its team's type.
- **The object (+0x318):**
  - the hero clash, Cinematic View and the interesting-object templates move it (off on the old object, on on the new);
  - End_Cinematic_Mode stops it;
  - the bombing run and the TSW shot leave it.

The port: `GameAudioHost::cinematic_engine_loop`, driven by `App::engine_loop_to`. Like retail, it moves only to a
different object. [port] A squadron member uses its own type, where retail uses its team's, so a selected fighter's
loop can differ (see the tier list).

## Movie Mode (slot 78), on key up: `0x2cc080` [H]

After the gates:
- **State 3:** the next hero clash after +0x358, wrapping, and never the same one. With none left, it tries the bombing
  run, then (if +0x360 is set) another interesting object. If all fail, the shot runs on.
- **State 2 or 4:** stop.
- **States 0 / 1:** stop (the reset), then try in order:
  1. the first hero clash (`0x4a4ac0`);
  2. the land bombing run (`0x49f370`);
  3. the interesting object (`0x2cd930(ctrl, 1)`).

  On success it sets state 3 and +0x34a.

The key down in state 3 sets the hold +0x317; in state 4 it is swallowed.

### The hero clash shot `0x4a4ac0(ctrl, mode, clash)` [H]

- **The participant:** clash vf+0x30 fills the participants [M]. The first one that is not dying
  ((+0x3a0 & 2) == 0 [L]) and is owned by the local player. If there is none, the shot fails.
- **The radius:** R = 7.5 × avg(E), where E is the half-extents. It is clamped to 125..500, or to 250..1000 in mode
  type 2, a branch that is dead because space has no clash list.
- **The keys:**
  - eye A: frame 0, hold, attached, offset (0, R, 0.67R);
  - eye B: frame 45, orbit, the same offset, period 600, reverse = an unsynced rand(0, 1);
  - look: frame 0, hold, on the hero.

  So the eye holds 1.5 s, then circles the hero at 18°/s, one turn per 20 s.
- **The length:** none. `0x4ad3a0` sets 0x40000000 frames. The shot runs until input, the next key up or the clash's
  removal.
- **The rest:**
  - `0x2cbc40(ctrl, clash)`, called from `0x56cda0` when the clash is removed, chains to the next clash or stops;
  - the letterbox `0x44b550` (0.5 s);
  - cinematic mode on (`0x49b1a0`: the camera saved once, near / far 1 / 10000, the HUD hidden);
  - the handheld noise `0x4ab780` [M];
  - the engine-loop SFX on the hero (+0x318, type +0x1420 / +0x1520).

### Not ported yet

- **`0x49f370`, the land bombing run [H].** It needs the local player's active bombing-run record (mode +0x38). The eye
  chases the bomber at offset (15, 60, 30), linear, until frames − 30. It then orbits the target marker at
  (300, 300, 250), period 600, clockwise. The look is fixed on the target. The length is 600 + FPS × frames [M: a retail
  seconds / frames mix-up]. The shot ends with the run (+0x32c).
- **`0x2cd930` → `0x4a53f0`, the interesting object [H].**
  - With `first` set: a random object of the local player's selection.
  - Otherwise `0x2cd0d0`:
    - with chance 2/3, the current object's target;
    - otherwise one of the first ten candidates ranked by `0x520df0`;
    - then, with chance 2/3, a random live hardpoint.
  - `0x4a53f0` then dispatches to about 10 templates by category: the type's own cinematic files (`0x4a3b40`), the
    hardpoint shot `0x4a47d0`, fighter `0x4a0140`, infantry `0x4a4e30`, frigate / corvette `0x4a26f0`, vehicle
    `0x4a6dd0`, flying `0x4a23d0`, and others. None is decoded yet.
  - In a battle without a hero clash or a bombing run, including every space battle, retail's Movie Mode lands here.
  - [port] With no clash the port frames nothing, which is what retail does when every pick fails.
- **`0x4a30b0`, a galactic-map tour,** reached from `0x2cc780` [H]. It is never a tactical shot.

## Cinematic View (slot 79), on key down: `0x2cbd00` [H]

- **Opening:** outside state 2, with In_Game_Cinematics and mode type 1 or 2, it opens view 0 (`0x4a6790(ctrl, mode,
  0)`). It then sets state 2, +0x34a, +0x354 = 0, and centres the cursor. The key is always swallowed.
- **Stepping:** in state 2, +0x354 += 1, wrapping 8 → 0, and the view opens again. If that fails, +0x34a stays clear
  and the next key clears the state.

`0x4a6790(ctrl, mode, view)` [H]:
- **The target:** `DAT_140b27d98` [L: a forced target], else the local player's first selected object, which must
  belong to the local player.
- **The geometry:** d = clamp(3 × avg(E) × zoom, 3|E|, 10|E|).
  - The yaw is 0, 315, 270, 225, 180, 135, 90, 45 for views 0..7, so the views step clockwise.
  - The pitch is 30° in space and 45° on land.
  - The eye offset is Rx(90) · Rz(yaw) · Rx(pitch) · (0, 0, −d) [M on the exact axes].
- **The keys:** one attached linear eye key and one look key on the object, so both are held and follow it.
- **The rest:** smoothing 0.9, no length, the letterbox, cinematic mode on, the handheld noise and the engine-loop SFX.
- Each view resets the shot clock (`0x4ad3a0`), and the smoothing only runs when the clock is above 0, so a view step
  cuts to the new view instead of gliding [M].

Keys in a view (`0x2cbd00`):
- View 0: Esc stops, and other keys pass through.
- A view above 0: every key except Ctrl (0x11) stops the cam and is swallowed.
- Slots 77 (tether) and 78 (Movie Mode) pass through. The Movie Mode key up then stops the cam (state 2).

The mouse in a view (`0x2cc3e0`):
- A move turns the view by its dominant axis / 4: pitch += dy / 4 (clamped to ±80), or yaw −= dx / 4 (wrapped).
- The cursor is re-centred and the eye key rebuilt (`0x4ac830`).
- The wheel zooms (`0x4ac520`) [H]: d = 3 avg(E) × zoom − 25 × notches (away = closer), clamped to [3|E|, 10|E|], and
  zoom (+0x1a8) = d / (3 avg(E)). The factor starts at 1.0 when the process starts and is never reset, so it carries
  over to the next view, object and battle. The first notch after a fresh view starts from the unclamped 3 avg(E),
  which is under 3|E|, so it usually lands on 3|E|.
- Mouse input never ends a view.

The port: `cinematic_view_shot` and `BattleCam` (`battle_cam.hpp`):
- The wheel zoom is `BattleCam`'s, kept for the App's lifetime (#564).
- [port] The offset is read as (−d cos p sin yaw, d cos p cos yaw, d sin p) in the object's frame, with view 0 on its
  +Y side, as the hero clash shot's eye is [M].
- [port] There is no cursor re-centring: the turn is the cursor's move since the previous frame.
- [port] A view that cannot be framed when stepping stops the cam.
- [port] Mouse buttons and the wheel are taken by the view (they do not reach the HUD or the world) [M: retail's
  `0x2cc3e0` returns before the world sees them].

## Input while the battle cam runs (`0x2cc3e0`, state 3) [H]

- A mouse move is swallowed when it is under 1/20 of the screen width AND height away from the **previous** cursor,
  because the baseline moves with every move. A larger jump stops the shot.
- Any button or wheel event stops it.
- Esc (key down) stops it, and other keys pass through.
- [port] The port measures the move per frame, not per mouse event, so a fast flick ends the shot sooner than in retail.

The TSW firing shot is state 3 too. `0x2ccf30` sets the baseline at Fire, and every move moves it; the port's
`TswFiringShot::input_cancels` does the same (#564).

## The per-frame update (`0x2cd870` → `0x4aaf00`) [H]

- It needs In_Game_Cinematics, +0x34a and a state.
- It slides the letterbox in.
- When the shot's timer expires (and +0x32c is clear):
  - outside state 3, it ends;
  - while the key or **Tab** is held, the shot is kept;
  - otherwise it chains: the next clash, the bombing run, then the interesting object.
- The hero clash and Cinematic View shots have no length, so none of this chaining reaches them.

## `b_camera_t` (`0x347100`) [H]

- If the mode is tactical and `0x2ce420` finds the camera's **pitch (+0x10) or yaw (+0x14)** off Pitch_Default /
  Yaw_Default, `0x2cf880` snaps them back. Distance and FOV are kept.
- Otherwise it calls `0x2cca80(ctrl, 0)`: the key-up start path, without the dialog, ENABLE_COMBAT_CINEMATIC,
  LOCK_CONTROLS and story-lock gates. In state > 1 it stops instead, so the button is a toggle and never cycles.
- [port] The free-rotation pitch offset and roll count as pitch (`TacticalCameraController::pitch_yaw_at_defaults`).

## The port's differences and their tier (`docs/SIMULATION.md` "Parity tiers")

All of them are presentation: no sim state, no synced draw, so tiers 2 and 3 are untouched. Camera feel is tier 1, so
each of these is a known gap rather than an accepted difference:
- The not-ported Movie Mode picks: with no clash, Movie Mode frames nothing (#562, #563).
- The mouse is measured per frame, not per event.
- There is no cursor re-centring in a view.
- A failed view step stops the cam.
- The cam stops when the framed object is no longer drawn.
- View 0's side [M].
- A squadron member's engine loop uses its own type, not its team's.
- The wheel zoom keeps its factor when the viewed object is gone (retail clamps it to [0, d]).

Tier 3 (internals): the clash identity (kind + first participant), and the battle cam and TSW shot holding their state in
separate classes.

## The host (port)

`App::battle_cam_sync` (`app_cinematic.cpp`) applies cinematic mode on and off:
- On: the mode camera is saved once, the HUD and the cursor are hidden, the letterbox slides in, and the presentation
  rate lock is taken (FS +0x37, 30 ticks/s, as the TSW shot's cinematic mode on).
- Off: the camera is restored, the lock is released, and the letterbox slides out.
- Overview (slot 76) is refused while the cam runs.
- [port] The cam stops when the framed object is no longer drawn.
- [port] A clash is identified by its kind and first participant. The list is the melee clashes, then the action
  clashes, in start order.

## Stop (`0x2cbb80`) and End_Cinematic_Mode (`0x49b5c0`) [H]

The letterbox slides out, the mode camera is restored, the shake / effect is cleared and the engine-loop SFX stops. The
state, the view, the clash and the interesting object are reset. A TSW Fire stops a running battle cam, and a battle-cam
start stops a running TSW shot.

## Open

- The interesting-object shot: the pick `0x2cd0d0`, its ranking `0x520df0`, and the ~10 templates `0x4a53f0`
  dispatches to (#562).
- The bombing-run shot `0x49f370` (#563).
- The exact axes of `0x4a6790`'s rotation.
- `0x49a610` (polar interpolation), the look smoothing `0x4a96d0`, the roll keys (#562: only its templates use them).
- State 4 (script cinematics) and the story START cinematic mode (+0x348).
