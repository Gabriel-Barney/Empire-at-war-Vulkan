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
- the static interesting-object shot, with dur 60, and the infantry push-in, with its length. The attack run, the
  hardpoint and fighter templates use it too (#570, #571); the orbit, the fly-past, the drive-by and the land flying
  shot don't.

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
- **`0x4a30b0`, a galactic-map tour,** reached from `0x2cc780` [H]. It is never a tactical shot.

## The interesting-object shot (#568)

The port: `interesting_pick`, `saliency_rank` and `interesting_shot` (`battle_cam.hpp`), with
`App::interesting_object_shot` (`app_cinematic.cpp`) reading the drawn objects and the sim.

### The pick: `0x2cd930(ctrl, first)` [H]

- **`first` (a Movie Mode key up from states 0 / 1, or `b_camera_t`):** a random object of the local player's
  selection, with no filter, so a structure can be picked. +0x368 is left alone, and no hardpoint is drawn. An empty
  selection falls to the next step.
- **Otherwise, `0x2cd0d0`:**
  - **A, validate (+0x368):** with no current object, +0x368 = 0. Otherwise +0x368 is set from the current object when
    it is 0, and when the id no longer resolves to the current object, both are cleared.
  - **B, the target, chance 2/3** (`rand(0, 2) > 0`): the current object's target, when it is not hidden, not fogged
    for the local player, has a model, and is neither a TEAM (0x16) nor a TRANSPORT (5). No hardpoint is drawn.
  - **C, the candidates:** every object with a locomotor, of any owner, that is not a wall (CategoryMask), not
    flying on land (`FlyingLocomotorBehaviorClass`), not fogged, fully faded in (≥ 0.9), not hidden, and not a TEAM
    or TRANSPORT. Structures are reached only through B or the selection.
  - **D, the ranking** `0x520df0`, below.
  - **E, the choice:** k = min(n − 1, 10). Up to k draws of `rand(0, k)` into the ranking; the first that is neither
    the current object nor of its type (+0x370) wins. With none, entry 0. Then the hardpoint draw, `rand(0, 3) > 0`:
    a random live hardpoint with a target.
- It logs `Start_Interesting_Object_Cinematic: Type %s` and dispatches (`0x4a53f0`).

**The +0x368 alternation [H].** Only step A writes +0x368. B and E change the object but leave +0x368 on the old id, so
the next validation clears the object, and only a ranked pick (whose "not the current" test then compares against
nothing; the type test still bites) is possible. The step after re-arms +0x368. So the target branch is open on every
other chain step. The port keeps the rule (`InterestingState::stored`).

### The ranking: `0x520df0` [H]

One row per candidate, 7 columns, each with a GameConstants weight (`Saliency_*`, FoC defaults in brackets):

| column | value | weight | normalised |
|---|---|---|---|
| Size | \|E\|² of the box | Saliency_Size (1.0) | (v − min) / range |
| Power | the type's AI_Combat_Power | Saliency_Power (1.0) | (v − min) / range |
| X, Y | the world position | Saliency_X / Saliency_Y (0.5 each) | 1 − \|v − mean\| / range |
| Health | the health | Saliency_Health (1.0) | 1 − (v − min) / range: lower is more salient |
| Targets | how many candidates target it | Saliency_Targets (1.5) | (v − min) / range |
| Speed | the displacement per frame | Saliency_Speed (1.0) | (v − min) / range |

- A zero range divides by 1.
- **The spread weighting:** each normalised column is multiplied by its own spread (max − min of the normalised
  values), so a column whose values are all alike counts for nothing.
- The score is Σ weight × column, sorted descending.

### The dispatcher: `0x4a53f0(ctrl, mode, obj, hp)` [H]

It clears every key list and the shake trackers (`0x4a7a90`), but not the smoothing +0x270. Then, first success wins:

1. the type's Glory_Cinematics files (`0x4a3b40`, #572), which clear +0x315;
2. +0x315 = 1, the land terrain clamp, for every generated shot;
3. a drawn hardpoint with a target: the hardpoint shot (`0x4a47d0`, #571);
4. Fighter: the chase (`0x4a0140`, #570);
5. Infantry: **the push-in** (`0x4a4e30`);
6. `rand(0, 3) == 0`: **the static shot** (`0x4a17c0`);
7. Corvette or Bomber: `rand(0, 1) == 0` ? the attack run (`0x4a26f0`) : the chase (#570);
8. Frigate: the attack run (#570);
9. Vehicle: **the drive-by** (`0x4a6dd0`);
10. no locomotor: the structure shot (`0x49f880`, #570), and on failure step 12;
11. flying: **the land flying shot** (`0x4a23d0`); then, on land only, \|E\| < 30 ? the push-in : the drive-by, and on
    failure step 12 (in space, straight to step 12);
12. `rand(0, 1) == 0` ? **the orbit** (`0x4a0830`) : **the fly-past** (`0x4a1c90`).

Every template sets the shot length (frames), the letterbox and the engine loop on its object. A template that leaves
the smoothing unset inherits the previous shot's value (`BattleCamShot::smoothing` is optional); only
End_Cinematic_Mode zeroes it. The hero clash sets none, so a clash chained after an orbit or a fly-past runs at 0.9.

### The ported templates [H]

- **Static `0x4a17c0`:** ψ = randf(0, 360)°, φ = randf(20, 45)° (negated with chance 1/2 in space),
  d = randf(2\|E\|, 4\|E\|). The look holds on the world centre C, and the eye holds at
  C + d·(cos φ cos ψ, cos φ sin ψ, sin φ): a tripod where the object was. Length 60, handheld noise for 60.
- **Orbit `0x4a0830`:** t = smoothstep(clamp((\|E\| − 30) / 470, 0, 1)) and r = ±2\|E\|. The eye orbits (mode 3)
  at the attached, translate-only offset (r, 0, 1.25 Ez), period 600, ease out 0.25, reverse on a coin. The look
  holds on the object, translate-only. Length 90 + trunc(180 t), smoothing 0.9, no noise.
- **Fly-past `0x4a1c90`** (needs a model). With `rand(0, 1) == 0` in space, and the type neither `Nebulon_B_Frigate`
  nor `Alliance_Assault_Frigate`: under the hull, eye (0, −0.6 Ey, −Ez) → (0, 3 Ey, −1.5 Ez) over
  T = (t + 1)·8·FPS, looking at (0, −6 Ey, 0). Otherwise: across the top front, eye R·(2 Ex, ±2 Ey, 2 Ez) →
  R·(2 Ex, ∓2 Ey, 2 Ez) translate-only over T = (4t + 3)·FPS, with R the object's rotation at the start. Smoothing
  0.9, no noise.

- **Push-in `0x4a4e30`** (infantry, or a small land unit; needs a model): n = rand(120, 150), h = rand(30, 40) / 20,
  s = ±1. The eye moves on world points fixed from the start's transform M, M·(2s Ex, 2 Ey, 2h Ez) →
  M·(s Ex, 2 Ey, h Ez), ease out / in 0.25. The look is attached: (0, 0, 0.25 h Ez) → (0, −2 Ey, 0.1 h Ez). Length n,
  handheld noise for n, smoothing inherited.
- **Drive-by `0x4a6dd0`** (a vehicle, or a large land unit; needs a model): n = rand(60, 90), s = ±1,
  H = clamp(5 Ez, 50, 125), then the variant rand(0, 2). Every eye key is a world point M·(…) fixed at the start, so
  the vehicle drives through the frame; the second eye key eases in 0.25. Length 2n, no noise, smoothing inherited.

  | variant | eye, frame 0 → 2n | look, frame 0 → 2n |
  |---|---|---|
  | 0 | M·(3s Ex, 2 Ey, H) → M·(2s Ex, 0, Ez) | attached 0 → (0, −3 Ey, 0) |
  | 1 | M·(3s Ex, −3 Ey, H) → M·(2s Ex, −Ey, Ez) | attached (0, −Ey, 0) → 0 |
  | 2 | M·(2s Ex, −3 Ey, 7) → M·(2s Ex, −2 Ey, 7) | world M·(0, 0, 30) → M·(0, 0, 20), both ease out 0.25 |
- **Land flying `0x4a23d0`** (a flying locomotor; needs a model): n = rand(60, 90), s = ±1. The eye holds on the world
  point M·(s Ex, −Ey, Ez) beside the craft, and the look holds on it (ease out 0.25). Length n, no noise.

The look filter flag +0x1bd that the push-in and the land flying shot set only matters after a glory shot (#572).

Translate-only (+0x51) places an attached key at the object's position plus the offset in world axes, so the object's
turning does not swing the camera.

### The chain: `0x4aaf00` → `0x2cda20` [H]

When a timed shot expires:
- outside state 3, the cam stops;
- while the Movie Mode key (+0x317) or **Tab** is held, the clock freezes on the last frame (attached keys still follow);
- otherwise the next hero clash, the bombing run (#563), then, if the run has reached an interesting object (+0x360,
  which a chained clash shot keeps and End_Cinematic_Mode clears), the next pick (`0x2cd930(ctrl, 0)`). If all fail, End_Cinematic_Mode: the cam stops.

So, with no hero clash, Movie Mode runs object to object: on alternate steps, with chance 2/3, to the current object's
target, which follows the fight; otherwise to a high-ranked object of another type.

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
  - otherwise it chains: the next clash, the bombing run, then the interesting object. If all fail, the cam stops.
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
- The not-ported picks and templates: the bombing run (#563); the glory, hardpoint, fighter, attack-run and structure
  templates (#570 - #572) fail and fall through, so their objects get the static shot, the orbit or the fly-past. The
  3/4 hardpoint draw is made, but no hardpoint is picked, and its index draws are not made, so for an object with
  hardpoints the later draws differ from retail's (#571).
- The look-filter flag +0x1bd (set by the attack run, the push-in, the land flying shot and the fly-past) is not kept;
  it only matters after a glory shot turns the filter on (#572).
- The interesting object's fields, read from the port's objects: "in the world, not hidden" (+0x39f, +0x3a0 & 0x40) is
  drawn and not dying or contained; "a model" is drawable; fogged is hidden from the local player by the FOW; the
  ObjBox and the ModelBox are one box; the health is the drawn health; the speed is the movement's current speed; a
  FLEET's power is its type's, not its members' sum; the target count leaves out the hardpoints' targets.
- The first pick takes the selection's drawn objects of the local player, where retail takes the selection unfiltered.
- Equal saliency scores keep the candidates' order; retail's introsort is not stable, so a tie may pick another object.
- The static shot's centre is the object's position, not its box centre [M].
- The land flying shot reads the ModelBox in retail; the port has one box.
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

- The interesting-object templates not ported yet (#570 - #572) and a second object per key (the attack run's and
  the structure shot's target).
- The bombing-run shot `0x49f370` (#563).
- The exact axes of `0x4a6790`'s rotation.
- `0x49a610` (polar interpolation), the look smoothing `0x4a96d0`, the roll keys (#562: only its templates use them).
- State 4 (script cinematics) and the story START cinematic mode (+0x348).
