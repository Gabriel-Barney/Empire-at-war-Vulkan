# Starbase production and the fleets in orbit (#433)

Retail FoC x64 `StarWarsG.exe` (v1.121.859328), read in the Ghidra export (reference only, not copied) on 2026-11-24.
Follows legacy docs/re/BKG1_RE_ANSWERS.md §10 (b0121), which read the first test and left the fleet byte +0x94 at [L].

## Two tests, one rule [H]

Retail checks the fleets in a planet's orbit in two places before a starbase is built:

| Where | What it does |
|---|---|
| Planet production service 0x402840, head rule 2 | A queue head of a DUMMY_STAR_BASE type (flag 0x5C), at a planet not in an active battle (0x2b0470), is cancelled with no refund when the orbit holds a matching fleet. It logs "starbase in production and enemy fleet in orbit", raises planet signal 3 and starts the next entry. |
| Buildability 0x2804d0, the starbase step (STRATEGIC_GAPS §1.3 step 20) | The same orbit walk makes a DUMMY_STAR_BASE type unbuildable at that planet (reason 10). |

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
spawn has it clear. What happens to the byte when such a fleet merges into another, or another fleet merges into it,
was not read [L].

## Port consequence

- `GalacticSim::starbase_orbit_blocked(planet, player)` is the one predicate for both tests: an alive fleet in
  orbit, owner != player, not hidden. `service_production` rule 2 and `build_check` step 20 use it.
- `enemy_fleet_in_orbit` (hostile and combat-capable) stays for its other callers, `orbit_conflict` (the
  enter-orbit test 0x4338d0) and `blockaded`, which are separate retail tests.
- The +0x94 byte is not modelled: every port fleet counts. Porting it needs a fleet flag that the galactic save
  carries, cleared on the fleet of a magic spawn, and the merge rule above (follow-up issue).
