# Weapon clip gates (WeaponBehavior fire service 0x54d090)

Issue #263 (split from #44 t05). Source: the Ghidra decompile of StarWarsG.exe (0x54c910, 0x54d090 and the helpers
named below), with the ModelClass slot layout of legacy docs/re/ANIM_PARTICLES.md §A.3.

Tiers: **[H]** read directly in the code, **[M]** inferred from the code with a gap named, **[L]** a guess.

## 1. Where the animation tests are

#44 (its R3G anim-timed note, §9) put the animation hold (flinch, crossfade, crouched move) in the gate
0x54c910. It is not there [H]: 0x54c910 holds no clip test. Its conjunction, in order, against the port
(`CombatSystem::unit_weapon_allowed` steps 1-6, then step 7 in `fire_unit_weapon`):

| # | 0x54c910 | Meaning | Port |
|---|---|---|---|
| 1 | 0x4c3700 (`+0x3c == 0` of an object whose pointer the decompile loses) and the static byte 0x140a43a88 | the global weapons switch [M: the object not named] | `cfg_.weapons_enabled` |
| - | obj +0x3a0 bit 0x10 clear | an object flag, **not named** [L] | **not tested** (open) |
| 2 | 0x39a400 and 0x39b2a0 not true | for a type with +0xd1 (Deploys) and a locomotor (slot byte +0x333): the locomotor's vf +0x130 / +0x128, the two deploy-transition states [M: vf names] | `deploy_transition` |
| 3 | 0x39a390 picks type +0xae or +0xaf | deployed (same guard; locomotor +0x2b3 set, or its vf +0x120) → Fire_Weapon_When_Deployed, else _Undeployed [H] | `deployed_or_deploying` |
| 4 | 0x39b950(obj, 0xb, 1) picks type +0xb0 or +0xb1 | ROCKET_ATTACK (11) active → Fire_Weapon_When_In_Rocket_Attack_Mode, else _In_Normal_Attack_Mode [H] | `ability_type_active(kAbilityRocketAttack)` |
| 5 | 0x397f60(obj, 1) | the WEAPONS_ENABLED flag ANDed over the active modes of ability entries 0 and 1 (0x565970) [H] | `ability_state.flag[kFlagWeaponsEnabled]` |
| 6 | locomotor (obj +0xa8) +0x48 == 0x1c, or == 0x16 when the locomotor's vf +0x240 is true; skipped when type +0x8a (Can_Fighter_Fire_When_Idle) is set | fighter idle / hover rest [H for the states; M for the vf] | `kUnitFighterIdle` / `kUnitHoverRest` |
| 7 | TD = obj +0xf0 | see below [H] | `fire_unit_weapon` step 7 |

Step 7: when obj +0x369 (LOBBING_SUPERWEAPON) is set and TD +0x3d (the charged fire-at-position order) is clear, hold.
Otherwise fire when TD +0x3d or TD +0x3c is set. Otherwise TD +0x18 must be 4. Any other value holds without a write.
At 4, fire when 0x54c190 finds the weapon target; with none, TD +0x28 := the weapon's Projectile_Fire_Pulse_Count
(+0x4f0) and hold. [port] The port's step 7 resets the burst on every non-4 state too, not only on a missing target.

The clip tests are in the fire service 0x54d090 itself, after 0x54c910, after the target and weapon lookups and before
the shot (0x54ad30) [H].

## 2. The helpers

| Helper | Reads | Meaning |
|---|---|---|
| 0x3956a0(obj) | ModelClass (obj +0x2a0) +0xc4 when the primary clip +0xb8 is set, else -1 | current clip type [H] |
| 0x395b10(obj) | ModelClass +0xf4 | type of the previous slot (+0xe8, the clip fading out; slot +0x0c = type) [H] |
| 0x397040(obj) | ModelClass +0x108 | a crossfade is running (the previous slot's timer flag) [H] |
| 0x27b590(type) | type - 0x4f < 8 | flinch clip, types 79-86 [H] |
| 0x38f9e0(obj, t) | 0x27b480(anim set +0xb0, t, 0), then vf +0x28 of that clip | non-zero when the model has a variant 0 of type t [M: vf +0x28 not read; the port reads it as `variant_count(t) > 0`, as anim_behaviors.cpp already does for 0x42 and 6] |
| obj +0x3a7 | byte | garrisoned or in limbo (`kUnitContained`) [H] |
| obj +0x33d | behaviour slot byte (+0x332 + id) of TURRET (0x0b) | -1 = no TURRET behaviour [H] |
| obj +0x210 +8 | GARRISON_UNIT data, state | 7 = inside a garrison (GarrisonDataPack, `garrison::State::Inside`) [H] |

## 3. The rule

Let `cur` be the current clip type and `prev` the previous slot's type. The own weapon holds fire this service when:

1. **Crossfade gate** [H]: a crossfade runs (0x397040) and the object is not contained (+0x3a7 == 0), and either
   - `cur` is ATTACK (5), ATTACKIDLE (6) or CROUCHMOVE (0x43 = 67), and `prev` is set (≠ -1) and is neither ATTACK nor
     ATTACKIDLE; or
   - `cur` is a flinch (79-86).
2. **Pose gate** [H]: the object has no TURRET behaviour (+0x33d == -1), is not contained, its GARRISON_UNIT state is
   not 7 (or it has none), the model has an ATTACKIDLE clip (0x38f9e0(obj, 6) ≠ 0), and `cur` is IDLE (0), MOVE (2) or
   WALKMOVE (0x42 = 66). The test is `(cur & ~0x42) == 0 && cur != 0x40`.
3. **Crouched-move cone** (already ported, MCX4_T11_T12_RE_ANSWERS §B4): `cur` is CROUCHMOVE with a fire target, and
   the target is more than Crouch_Move_Fire_Angle_Cutoff off the facing.

In words: a unit fades into an attack pose before it fires. While it fades from anything but an attack clip into
ATTACK, ATTACKIDLE or CROUCHMOVE, or into a flinch, it holds fire. A model that has an attack-idle pose does not fire
from its idle or walk/run clips at all.

## 4. What changed in the port

- `anim::weapon_clip_hold(world, id, contained, pose_exempt)` (anim_state.hpp) is rules 1 and 2.
- `CombatHooks::clip_holds_fire(id, contained, turret)` is called in `fire_unit_weapon` before the crouch cone, own
  weapon only. The battle host answers it from its animation world; TURRET or a GARRISON_UNIT state 7 exempts rule 2.
- **The flinch refusal moved.** `TacticalBattle`'s `shot_allowed` refused every shot, hard points included, while the
  current clip was a flinch (legacy docs/re/ANIM_PARTICLES.md A.8). Retail tests the flinch only in 0x54d090, own
  weapon only, and only while a crossfade runs (rule 1). The hard-point fire Fire_At 0x3825b0 has no clip test: the
  only callers of 0x27b590 are 0x54d090 and the targeting functions 0x569400 / 0x569540 [H].
- With the targeting service's ATTACKIDLE play (blend 0.2-0.75 s, `attack_idle`), a stationary unit now holds fire
  for that fade before its first shot.

## 5. Open

- obj +0x3a0 bit 0x10 (§1) is not named and the port does not test it.
- The flinch hold is narrower than "while it flinches": rule 1 holds a flinch only while its crossfade runs [H], so a
  unit in a flinch whose fade has ended can fire.
- The flinch's own crossfade: rule 1 holds a flinch only while its fade runs. Whether the port's flinch keeps the
  crossfade running through Max_Flinch_Hold_Seconds is the anim system's model (#361 / #362), not re-checked here.
- 0x569400 / 0x569540 (targeting) also call the flinch test; not part of this rule.
