# Starbase production and the fleets in orbit (#433)

Retail FoC x64 `StarWarsG.exe` (v1.121.859328), read in the Ghidra export (reference only, not copied) on 2026-11-24.
Tiers: **[H]** read directly in the code, **[M]** inferred from the code with a gap named, **[L]** a guess.
Follows legacy docs/re/BKG1_RE_ANSWERS.md §10 (b0121), which read the first test and left the fleet byte +0x94 at [L].

## Two tests, one rule [H]

Retail checks the fleets in a planet's orbit in two places before a starbase is built:

| Where | What it does |
|---|---|
| Planet production service 0x402840, head rule 2 | A queue head of a DUMMY_STAR_BASE type (flag 0x5C), at a planet not in an active battle (0x2b0470), is cancelled with no refund when the orbit holds a matching fleet. It logs "starbase in production and enemy fleet in orbit", raises planet signal 3 and starts the next entry. |
| Buildability 0x2804d0, the starbase step (legacy docs/re/STRATEGIC_GAPS.md §1.3 step 20) | The same orbit walk makes a DUMMY_STAR_BASE type unbuildable at that planet (reason 10). |

Both walk the planet's orbit object list (planet +0xC8, count +0xD0). An object matches when all of these hold:

1. it has fleet data (object +0xC0 non-null): it is a fleet;
2. it is not being removed (object +0x3A0 bit 2 clear);
3. its owner id (+0x58) differs from the building player's id. This is a plain compare: there is no ally or
   hostility test, so an allied or a neutral fleet matches too;
4. it is not stealthed (0x3a9760, the port's `GalacticSim::hidden`);
5. its fleet data byte +0x94 is set (rule 2 tests == 1, the buildability step tests != 0).

There is no `ai_combat_power` test and no test of what the fleet holds.

## The fleet data byte +0x94 [M]

- `FleetDataPackClass`'s constructor (0x5528e0) writes the 16-bit word at +0x94 as 1, so a new fleet has the byte
  set [H].
- The only writer found that clears it is the AI build task's HIJACK_UNIT state (AIBuildTaskClass 0x648370). After a
  magic spawn it clears the byte on the spawned unit's fleet (the unit's own fleet data, or its parent's when the
  unit is carried) [H for this writer; M that no other writer exists: the search covered the decompiled one-byte
  and two-byte stores to +0x94 and the fleet-data pointer stores near it].
- The only readers found are the two tests above [M, same search].

So +0x94 reads as "this fleet counts against starbase builds in its orbit", and only a fleet formed by an AI magic
spawn has it clear.

## Writers and merges (#456, read 2026-11-25)

- A capstone sweep of every function for a store covering +0x94 (byte to xmm width) and for `lea +0x94` found these
  writers on fleet data [H for each]:
  - the constructor 0x5528e0 (the 16-bit store of 1; its only caller is 0x390790, reached from FleetBehaviorClass vf7
    0x431800; there is no copy constructor or assignment, and the destructor 0x552970 leaves it alone);
  - HIJACK_UNIT 0x648370 (the byte store of 0 at 0x6487f1);
  - the load 0x552b90 (microchunk 0xF, one byte; the save 0x552e10 writes the same chunk).
  Every other hit belongs to an unrelated class. So HIJACK_UNIT is the only clearing writer [M, high: the non-fleet hits
  were classified by the stores around them, not by tracing each base register].
- HIJACK_UNIT, after the magic spawn 0x400b40, unless the planet is AI-blockaded (0x3fa370) or the object is itself a
  Galactic_Fleet, runs FleetManagement op 4 and then op 0 with no target on the spawned unit at its planet (0x5afb60
  field map) [H]. Op 0 with no target makes a new Galactic_Fleet holding the unit, and the clear lands on the unit's
  parent fleet (+0x2b0) [H for the calls; M that the unit ends alone in the new fleet].
- Merge 0x433dd0(A, B): with the same player, every unit of B (last first) leaves B (0x434d30) and joins A (0x430f30);
  the emptied B dies (0x391310). None of the add and remove functions touches +0x94 [H]. So A keeps its byte and B's
  dies with B. Its two callers are the final arrival 0x56a1e0 (the arriving fleet merges into the one in orbit) and
  FleetManagement op 3 0x5af260 (the object fleet merges into the target) [H].
- Consequences: a clear fleet merging into a set one gives a set fleet; a set fleet merging into a clear one stays
  clear; a unit moved into a clear fleet stays in a fleet that does not count; a unit split off into a new fleet gets a
  fresh 1 [M].

## Port consequence

- `GalacticSim::starbase_orbit_blocked(planet, player)` is the one predicate for both tests: an alive fleet in
  orbit, owner != player, not hidden. `service_production` rule 2 and `build_check` step 20 use it.
- `enemy_fleet_in_orbit` (hostile and combat-capable) stays for its other callers, `orbit_conflict` (the
  enter-orbit test 0x4338d0) and `blockaded`, which are separate retail tests.
- #456: the byte is `FleetState::counts_for_starbase` (true for every new fleet), tested by `starbase_orbit_blocked`.
  The galactic build task's magic plan (engine/ai/src/galactic_build.cpp) follows HIJACK_UNIT: after the spawn, unless
  the planet is AI-blockaded, a surface unit is launched and formed as before, and a unit spawned into an orbit fleet
  is formed alone with FleetManagement op 0 (so slotting and the new-fleet conflict run as for any op 0). Then
  `GalacticSim::clear_starbase_count` clears the flag on the unit's fleet when the unit is alone there. The merges
  (`merge_fleet`, FleetManagement op 3, the arrival merge, the AI's READY_TO_MERGE) already keep the receiving fleet's
  state, as retail does. Galactic save v15 carries the cleared fleets.
- [port] gaps:
  - a launched ground unit that stays in the shared orbit fleet (the port has no company transports) leaves that
    fleet set; one formed alone (it carries itself) is cleared;
  - a blockaded orbit spawn does not fail the task as retail's state 7 does (as before), but it is not formed or cleared;
  - the tactical reinforcement hijack (engine/apps/sim/src/battle_ai.cpp `hijack_unit`) does not form or clear: the
    reinforcement stays in the battle's orbit fleet.
