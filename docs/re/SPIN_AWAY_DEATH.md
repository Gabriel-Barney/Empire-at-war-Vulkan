# Spin-away death wrecks

Retail FoC x64 `StarWarsG.exe`, read in the Ghidra export and, where the export dropped arguments, in the disassembly
(#357, 2026-11-24). This replaces TACTICAL_GAPS §1.9 "Spin-away motion is simulation state" of the legacy notes.
Tiers: **[H]** read directly, **[M]** inferred with good support, **[L]** guess. The port is
`engine/apps/sim/src/tactical_battle_spin.cpp` (`TacticalBattle::service_spin_wrecks`), with the death plan in
`schedule_death_clone` and the explosions in `spawn_death_explosion_start` / `spawn_death_explosion_end`.

Notation:
- `obj` is the object that holds the DeathBehavior. That is the death clone, because `0x29f270` attaches DEATH to it.
- `T` is its type (`obj +0x298`), and `D` is its DeathDataPack (`obj +0x1d0`).
- FPS is the int global `0x140b0a340` (30), converted to float.
- All arithmetic is `float`.

## DeathDataPack (the spin fields)

| D + | Meaning | Save chunk (write 0x58c240 / read 0x58c030) | Port (`TacticalBattle::SpinWreck`) |
|---|---|---|---|
| 0x20 | `SplineClass*`, the wreck path | 3: each control point (12 bytes) | `pts` (the spline is rebuilt from them) |
| 0x28 | travelled distance | 4 | `dist` |
| 0x2c | roll accumulator, also the "first service done" marker | 5 | `acc` |
| 0x34 | spin flag | 6 | the wreck exists |
| 0x38..0x40 | V, the inherited velocity (units / s) | 2 | `v` |

The load `0x58c030` re-appends the saved points in order and calls `Build(20)`. The coefficients and segment lengths
are recomputed, not saved [H]. The port does the same in `rebuild_spin_paths` (battle state version 25). The clone's
roll, pitch and yaw (`obj +0x84..+0x8c`, degrees) are saved with the wreck, because the port's object store has only a
position and a facing.

## Death: DeathBehavior Initialise 0x58cdb0 [H]

1. Play the death clip `0x3a92f0`. Retail plays it even when spinning.
2. With `Spin_Away_On_Death` (+0x9a): one synced `Get_Float(0, 1)` (0x1ffbb0). Spin when the draw <= `Spin_Away_On_Death_Chance` (+0x2208, default 0). The draw is made for every chance, so a chance of 0 spins only on a draw of exactly 0.
3. `0x58cf90` phase 1 when spinning, else phase 0.
4. Without a clip and without a spin: removal `0x391310`.

Phase 1 is the same block as phase 0, with these differences:
- It reads `Spin_Away_On_Death_Explosion` (+0x2210, a type list).
- It picks with a synced `Get_Int(0, n - 1)`, with no draw for n = 1.
- It spawns at `0x398440(obj)` = position + (0, 0, `Ranged_Target_Z_Adjust`).
- It plays `Spin_Away_On_Death_SFXEvent_Start_Die` and does not play `Death_SFXEvent_Start_Die`.

V is written by the clone spawn `0x29f270` [H formula]:
- With a locomotor pack: `V = (FPS (cur.x - prev.x), (cur.y - prev.y) FPS, (cur.z - prev.z) FPS)`. `cur` and `prev` are the pack's `+0x164` / `+0x194` matrix snapshots, which the object service copies at its start.
- Without a pack: V = 0.

Port:
- `snapshot_unit_motion` keeps `Unit::snap0` / `snap1` for the movable units once per tick, before the movement.
- `death_motion` turns them into V. [M] for the frame alignment.
- The anim plan makes the spin draw; `plan_death` uses the `<=` rule above.
- [port] The death clip is not played on a spinning clone.

## Service 0x58d430, spin branch [H]

```
if D.acc == 0.0f:                       // ucomiss: -0.0 too
    build_path(obj)                     // 0x58c7a0: one synced theta when |V| != 0
    D.acc = obj.roll
speed = sqrtf((V.x V.x + V.y V.y) + V.z V.z)
step  = speed / FPS;  limit = speed x T.Spin_Away_On_Death_Time (+0x220c)
D.dist = step + D.dist                   // stored before the tests
if D.dist >= limit or step == 0 or no spline or !sample(D.dist, P):
    phase 0 (Death_Explosions, pick and lift, then Death_SFXEvent_Start_Die)
    delete the spline; remove_from_world(obj, 1)       // the destructor then runs phase 2 (#221)
else:
    A = angles_to(obj.pos, P)            // 0x20acd0 (roll 0)
    Turn_Toward(obj, A, -1.0f, yaw, pitch)  // 0x3abe40: per axis wrap180, clamp to the turn rate, add_angles
    D.acc = D.acc + 20.0f; obj.roll = D.acc   // 0x8007e8
    set_position(obj, P)                 // 0x3a8f90
```

- The turn rate is `0x372560`: `Max_Rate_Of_Turn` (+0x38c) × `Object_Max_Speed_Multiplier_{Space,Land}` × 1.0, in degrees per service. [port] The type flag `+0x790` that skips the multiplier is not read.
- `add_angles` `0x20b070` makes one correction per axis. Pitch: >= 360 → −360, <= −360 → +360. Yaw: >= 360 → −360, < 0 → +360.
- The roll is never wrapped. With a starting roll of −20 n, `acc` returns to 0 after n moves and the path is built again. The port keeps that.
- Service count: K is the first k with `dist_k >= limit`, using the same float sum. That is about ceil(30 T), but it can differ by one when 30 T is an integer.
  - Services 1..K−1 move.
  - Service K spawns phase 0 at the position of service K−1 and removes the clone.
  - V = 0 ends at the first service.
- [port] The first service is on the tick after the death. The clone's place in the object walk is untraced [L].
- [port] A land clone obstacle (`register_clone_obstacle`) is teleported with the wreck. How retail's land collision follows a moving clone is untraced [L].

## Path 0x58c7a0 [H, every constant and operand order checked in the disassembly]

```
if sqrtf(V.x² + V.y² + V.z²) == 0: delete the spline; return     // no draw
P0 = obj.pos;  E = P0 + Ts V (per component);  d = E - P0
m  = ((P0 + E) 0.5f) + 1.0f (per component)
c1 = cross(m, d);  c2 = cross(c1, d)     // c2 from the raw c1; both normalised when their length > 0
L  = sqrtf((d.y² + d.x²) + d.z²);  k = L 0.3f
P1 = (P0 + E) 0.5f
theta = Get_Float(0, 6.2831855f)          // one synced step; [0, 2 pi] inclusive
s, c  = sinf(theta), cosf(theta)          // theta == 0: s = 0, c = 1
P2 = (E.x + (s c2.x + c c1.x) k, E.y + (c c1.y + s c2.y) k, E.z + |c c1.z + s c2.z| k)
points P0, P1, P2, E; Build(20); D.dist = 0
```

The port uses `dmath::sin` / `cos` for the CRT's. As for the rocket spline, this is parity by behaviour, not by bits.

## SplineClass

SplineClass is the class the rockets use (`engine/tactical/combat/include/alamo/tactical/combat/rocket.hpp`,
`RocketSpline`). It is a natural cubic per axis, parameterised by index. Segment lengths are adaptive. `Sample(s)`
picks the first segment i with `L_i + cum > s` and evaluates at u = (s − cum) / L_i. The spacing 20 sizes only the
polyline table, which the death path never reads [H, M for "no other reader"].

## Synced draw order of a spin-away death

1. The death clip's random variant [M; the port draws none for a spinning clone].
2. The spin decision `Get_Float(0, 1)`.
3. The phase 1 pick.
4. The first service's theta.
5. The end service's phase 0 pick.
6. The removal's phase 2 pick (#221).

## Port-only paths

- **No clone.** A death without a clone object (kind 15, a failed spawn, the infantry corpse path) has no DeathBehavior holder to move. Its spin corpse (combat's draw, #356) stays an animation record and vanishes after `Spin_Away_On_Death_Time`.
- **Debris.** A debris clone (`sp_debris_expire`) takes V = 0.
- **No animation module.** A build without engine/anim plans no spin-away.
