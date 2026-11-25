# The galactic free store and a just-built unit (#401)

Retail FoC x64 `StarWarsG.exe` (v1.121.859328), read in the Ghidra export (reference only, not copied) and in retail
probe and capture runs on 2026-11-25. Tiers: **[H]** read directly in the code or the data, **[M]** inferred with a
gap named, **[L]** a guess.

## Why it matters

On HotM the port's Underworld moved newly built warships (INTERCEPTOR4_FRIGATE, CRUSADER_GUNSHIP, SKIPRAY_SQUADRON) from
their build planet on to Shola and Nal Hutta. These are the fast 79 / 88 / 100-tick legs of
`travel.hop_ticks[Underworld:SHOLA->NALHUTTA]`. Retail's Underworld leaves them where they were built: in the 9 HotM
captures, every Underworld leg is a hero-shuttle fleet.

## The only mover in vanilla galactic [H]

- In the shipped `GalacticFreeStore.lua`, `On_Unit_Added` calls `MoveUnit`.
- For a non-hero ship, `MoveUnit` calls `Find_Space_Unit_Target` (or `Find_Ground_Unit_Target`), then
  `FreeStore.Move_Object`.
- `On_Unit_Service` never runs (the `UnitServciceRate` typo; a retail probe logged no service call).
- `Find_Space_Unit_Target` starts with `object.Get_Planet_Location()` and returns nil, before any engine call, when that
  is nil.

## What retail's script sees for a just-built unit [H data, L cause]

- The a2f probe run (HotM, `tools/probe_mod/a2f_budget/out/20261108-212556` in the legacy tree) logs
  `Get_Planet_Location` inside `On_Unit_Added`:
  - a CRUSADER_GUNSHIP built at Felucia (frame 270): nil;
  - the UNDERWORLD_INFANTRY_DROID_SHUTTLE of a Destroyer Droid company built at Nal Hutta (frame 1066): nil;
  - every unit of the initial fill (frame 0): its planet.
- So the script finds no target and never moves a just-built unit.
- The q2 capture 20261115-220422 agrees: frigate 581, built at The Maw around frame 504 in a new fleet of its own, stays
  there to frame 13501.
- Why the parent chain is still empty when the admission runs is not read [L]. The "fleet of one" builder 0x401660
  appears to link ship, fleet and planet (0x4338d0, 0x430d90 -> 0x3a8f80) before it returns.

## The engine answers that depend on it [H]

- `Move_Object` 0x68f5b0: with no planet in the unit's parent chain, 0x68efb0 reports it in transit ("Unit is already in
  the middle of a move") and the call returns false.
- `Reachable_Target` 0x698360: argument 6 resolves through 0x6b8590 (the unit's or its fleet's planet, +0x2B0). A null
  source skips the reachability test entirely (the test of r15 at 0x698976), so slot 7 (0x663810) and its 1e18 are never
  reached. This matters for transports, whose ground search still evaluates the candidates and can make one synced
  random draw.

## Port (#401)

- A produced admission is flagged: `claim_units` (engine/ai/src/galactic_ai.cpp) admits a finished production either
  claimed by a build task with no destination or left by a task that has ended.
- While that unit's `On_Unit_Added` runs it is unplaced (`AiScriptHost::object_unplaced`):
  - Lua `Get_Planet_Location` and `Get_Parent_Object` return nil (the galactic container hook);
  - `planet_of_unit` gives no planet, so `Move_Object` refuses it (in transit) and a FindTarget source of it has no
    planet.
- `find_target`: a source that resolves to no planet skips the reachability test, as 0x698360 does. Before, it gave
  1e18 for every candidate.
- Other admissions (the initial fill, a unit a task force gives back, a free-store claim re-admitted) are unchanged:
  retail's script sees their planets.
- Not flagged: a just-built ground unit that fails its task because its build planet is blockaded
  (GROUND_UNIT_PRODUCTION 0x648120, then re-admitted). Whether retail's chain is still empty on that path is not read
  [L]; the port keeps its planet.
- The `find_target` change applies to every source with no planet, also a unit in a moving fleet: 0x698360's skip does
  not depend on the caller [H]; that a fleet in transit has no planet (+0x2B0 null) is [M].
- [port] Only the planet chain is hidden: the unit's fleet still lists it (`fleet_of_unit`, Lua `contents_of`). The
  shipped script reads only the planet.
