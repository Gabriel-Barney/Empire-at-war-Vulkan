# Galactic auto-resolve: one shot per combatant, hangar units (#193)

Retail FoC x64 `StarWarsG.exe` (v1.121.859328), read in the Ghidra export and the disassembly (reference only, not
copied) on 2026-11-25. Tiers: **[H]** read directly in the code, **[M]** inferred with a gap named, **[L]** a guess.
Extends legacy docs/re/BATTLE_TRANSITIONS.md §4.3-4.4 (the port's rules are in
engine/galactic/include/alamo/galactic/auto_resolve.hpp).

## Exchange of fire 0x60d2a0: one shot per combatant [H]

- Each combatant's three fire loops (the land planet's tactical-built list, the Starting_Spawned_Units entries, the own
  shot) share one flag, set to -1 at the combatant's start (0x60d725) and cleared after the first apply (0x60d7e8,
  0x60d90f, 0x60d9b8). So a combatant fires exactly once: the first of those that exists.
- A spawned entry fires at the P of **one** member, not count x P.
- The own shot uses the representative type (0x60c710). For the planet combatant in space that is the starbase type
  (BaseCombatantClass ctor 0x6cb700 stores the planet data +0x140 at +0x50; vf+0xE8 0x25b990 returns it).
- In space a TRANSPORT combatant (+0x337 != -1) fires no own shot.
- Land uses the same path (0x60b930 with +0x3d = 0). The land planet combatant fires only from its persistent
  tactical-built list (D+0x08, read through 0x4b8090), never its own type or spawned units.

Example, tatooine_space_day16: Empire_Star_Base_1 lists Starting_Spawned_Units_Tech_0 TIE_Fighter_Squadron 2 and
TIE_Bomber_Squadron 2, so the starbase fires one TIE_Fighter_Squadron (7 x 35 = 245), never its own 5000.

## Strength vector 0x60b0d0 [H]

Every non-planet combatant adds count x P(member) for each Starting_Spawned_Units entry at its owner's tech level
(type +0xAD0, falling back to lower levels), plus its own P(t) once. The land planet adds only the P of each D+0x08 type.

## Survival roll 0x60a140 [H]

- Called for side 0, then side 1 (0x60b930), not winner first.
- Pick: the first combatant holding a named hero (0x38fa20) or with behaviour 0x5f, else a synced int(0, n - 1).
- For a galactic object the budget first loses count x P(member) for each spawned entry, floored at 0, whether the
  combatant survives or not. Then a non-planet survives iff budget - P(t) x allowance > 0 and pays P(t); a planet takes
  the starbase downgrade.

## A type with members [H / M]

- A galactic company or squadron is one combatant: Add_Object 0x608080 recurses only into fleets and planets [H].
- Its P(t) is the sum of its Squadron_Units / Company_Units members' P, written into the type by the post-load pass
  0x377500 [H]. It counts once in the vector and the survival cost and fires once with that P. As one piece its
  categories are taken as the members' combined categories [M].

## Port (#193)

- `make_ar_combatant` (engine/galactic/src/conflicts.cpp): one own piece (a type with members: their summed P and
  combined categories, a company's survivors when it lost members); the spawned pieces at the owner's tech level; the
  one shot (the first spawned entry at one member's P, else the own piece).
- `auto_resolve` (engine/galactic/src/auto_resolve.cpp): spawned pieces in the vector, one `fire(shot)` per combatant,
  the spawned charge before each survival test, survival side 0 then side 1. Conflict save v5 carries the spawned
  pieces and the shot.
- [port] Not modelled: the space TRANSPORT shot skip (the port has no transport objects); the land planet combatant's
  tactical-built list. Ground structures are still land combatants here, but retail adds none (#558).
