# The direct-hit flinch: shields and direction

Issue #367 (part of #264 t07, gap-list row 6). Source: the Ghidra decompile of StarWarsG.exe.

Tiers: **[H]** read directly in the code, **[M]** inferred from the code with a gap named, **[L]** a guess.

## 1. Where the flinch is called

The flinch start 0x62dde0 has one caller outside itself: Apply_Direct_Damage 0x4c73d0 [H] (projectile `P`, victim `V`,
impact point `I`).

1. It calls Take_Damage 0x3a9e30 on `V` with a flag out-parameter `absorbed`, or queues delayed damage
   (Projectile_Damage_Delay_Secs). The delayed path leaves `absorbed` at 0.
2. Then, when its no-effects argument is clear and an impact point is given:
   - **`absorbed` == 0:** the hit-particle path. Its end: when `V` has an IDLE behaviour (slot byte +0x339 ≠ -1),
     `0x62dde0(V's IDLE behaviour, P type's Induced_Flinch_Intensity (+0x20e4), &P.pack +0x98)`.
   - **`absorbed` ≠ 0:** the shield-hit particle path, oriented along −(P.pack +0x98) normalised. **No flinch.**

## 2. What `absorbed` means

Take_Damage clears the flag, then sets it [H]:
- after the base-shield behaviour (0x39, slot byte +0x36b) absorbs `x` with x ≥ the damage left;
- or after the SHIELDED behaviour (0x10, slot byte +0x342) absorbs `x` with x ≥ the damage left.

A partly absorbed hit leaves it clear. So **a hit the shields absorb as a whole does not flinch; a partly absorbed
hit does**. The port's `HitEvent::absorbed` is the same condition (absorbed > 0, nothing to hull or hard point [M]),
and it already selects the shield-hit particle.

## 3. The direction

The vector is P's projectile pack (obj +0xe8) +0x98. The port names it LastKnownVelocity (`Rocket::last_vel`:
the last move's displacement × 1/FPS) [H for the offset]. The flinch's front / back / left / right choice reads its XY
against the victim's facing (`flinch_type`).

## 4. The port

- `HitEvent::travel` carries the projectile's LastKnownVelocity at the hit. A rocket gives its `last_vel`. A straight
  shot gives its direction: its last move is along it, and only the direction is read. A lobbed grenade would need
  its arc tangent [port: launch direction kept].
- `TacticalBattle::anim_hit` skips an `absorbed` hit and uses `travel` as the direction. When `travel` is zero it
  falls back to impact point → victim centre, then shooter → victim [port].
- Save format: combat version 21 adds the field to the saved hit events.
