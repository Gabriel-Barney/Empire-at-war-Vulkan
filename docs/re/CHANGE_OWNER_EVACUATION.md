# Change_Owner of a planet and the forces on it (#189)

Retail FoC x64 `StarWarsG.exe` (v1.121.859328), read in the Ghidra export and the disassembly (reference only, not
copied) on 2026-11-26. Tiers: **[H]** read directly in the code, **[M]** inferred from the code with a gap named, **[L]**
a guess. Follows legacy docs/re/BKC1_RE_ANSWERS.md Q3 (b0013) and GALACTIC_ECONOMY.md §4.2 "Forced transfers", which
left 0x4b7670 / 0x4b7860 untraced.

## The routine: Change_Owner 0x3f7280 [H]

`0x3f7280(this, planet, new owner)` is the planet's Change_Owner (Lua `Change_Owner` and the story CHANGE_OWNER reward).
In order:

1. When the planet's owner (+0x58) already resolves to the new owner, it returns false and does nothing.
2. **Every ground structure is removed** (0x4b7350 on the planet data D). It walks the ground-structure list D+0x158
   and removes each entry through 0x4bb4a0, which destroys the object (0x391310) unless it is already dying, whatever
   its owner. Then it recounts the ground base level (0x4bdaa0), which is now 0. Orbital structures (D+0x168) are not
   read anywhere in 0x3f7280.
3. With a starbase level D+0x148 > 0, the story event STORY_DESTROY_BASE is raised for the planet (0x327110: log
   "STORY - BASE DESTROYED on planet %s", event 7). A notification 0x26 is sent for the planet (0x220ed0).
4. The starbase is cleared (0x4bda40(D, 0): pointer D+0x140 and level D+0x148 to 0). The space queue is re-evaluated at
   level 0 (0x402660), and the starbase owner D+0x13c is set to -1.
5. **The branch on the old owner.** It compares the planet's owner id (+0x58) with the local player's id (0x294a40,
   +0x4c):
   - **The local player.** `0x2ba6c0(conflict manager, planet, old owner)`, then
     `0x2ba850(conflict manager, planet, old owner, 0)`. The third argument of 0x2ba6c0 is set at 0x3f7362
     (`mov r8d, [rsi+0x58]`); the decompiler drops it. Nothing else on the planet changes.
   - **Any other owner.** `0x4b7670(D, new owner, 1)` walks the surface list D+0xe0 and `0x4b7860(D, new owner, 1)` walks
     the orbit list D+0xc8, each last to first. Every object whose owner does not resolve to the new owner (an owner
     id that resolves to no player counts as not the new owner) is removed. First, each hero it holds leaves:
     - 0x38fa20 finds the object itself, or one it carries (fleet data, team), whose type has the hero byte +0x13;
     - with the flag 1, 0x48eb10(hero, no planet, 0) schedules the respawn at Default_Hero_Respawn_Time;
     - 0x48cf20 removes the hero, and its team parent when it has one.
     
     Then 0x391310 removes the object and the list entry is dropped.
6. The list at D+0xa0 is cleared (std::map erase, size D+0xa8 := 0).
7. The owner change 0x3fb040(planet, new owner id, 1.0f) runs, with the victory tests (BKC1 Q3). The routine returns
   true.

## The local player's orbit: 0x2ba6c0 [H]

The fleet half of the battle retreat (legacy BATTLE_TRANSITIONS §3.3a; the port's `ConflictManager::retreat_fleets`):

- The destination is the first hit of the finders 0x2a9dd0 (orbit controller), 0x2aa400 (ground base or starbase
  owner), 0x2a9ee0 and 0x2a8dd0, each excluding the planet itself. With no hit, it is the planet itself.
- The path is `[planet, destination]`. Every object in the planet's orbit list with that owner, not dying (+0x3a0
  bit 2), walked last to first, gets `0x39f380(object, path)`.
- **Which neighbour 0x39f380 picks.** None: it takes the path's last entry as given. It refuses an empty path, an
  object without movement behaviours (+0x333 / +0x336 == -1) and a fleet whose departure test 0x431c80 fails. Otherwise
  it plays `Galactic_Fleet_Hyperspace` when the local player can see it and starts the move (0x56a010). So the
  destination is the nearest planet of the first finder that has one. There is no trade-route or neighbour test (the
  port's `move_fleet_direct`).

## The local player's surface: 0x2ba850 [H]

The ground half (the port's `ConflictManager::retreat_ground`):

- It returns at once when the surface list D+0xe0 is empty.
- It creates a fleet for the player (0x29e740). Every surface object of that owner that is not dying leaves the surface
  (0x4f4690) and joins the fleet (0x430d90), with the log "%s: Retreating %s from surface.".
- An empty fleet is removed (0x391310) and nothing moves. Otherwise the fleet is placed in the planet's orbit (0x4338d0).
  Its destination is the first hit of 0x2a9dd0, then 0x2aa400 (the other order with the flag set), with no exclusion,
  then 0x2a9ee0 and 0x2a8dd0, then the planet itself. A fleet-move command (0x3af290 / 0x3af450: fleet id, destination
  id) is executed for it.
- **Units that cannot leave.** There is no transport, space or movement test. Every live surface unit of the player
  joins the new fleet in orbit. A unit that cannot travel stays in that fleet above the planet. The orbit-controller
  finder sees the new fleet (0x3f9270: the first unstealthed fleet in orbit), so with no starbase and no other fleet
  of the player in orbit the destination is the planet itself and the fleet stays there [M: the controller test of the
  new fleet's state 0x4327b0 < 3 was not traced].

## Port rule

- `GalacticSim::change_owner` on a planet follows steps 1, 2, 4, 5 and 7. The local branch calls
  `ConflictHooks::evacuate_owner_change`, which `ConflictManager` implements as `retreat_fleets`, then `retreat_ground`.
  Without conflict hooks nothing leaves.
- The other branch removes every surface unit and every orbit fleet not owned by the new owner, last first. A hero
  (`is_hero_type`: Is_Named_Hero / Is_Generic_Hero) inside leaves first through `hero_depart` (respawn scheduled at the
  default time, then despawned without a death event). The rest go through `destroy_object` [M: the hero byte +0x13 is
  read as the port's hero test].
- [port] A structure outside the ground family (retail keeps those in D+0x168, which 0x3f7280 never reads) is still
  converted to the new owner, so the derived owner (GE 4.1) keeps the new owner.
- The starbase stays destroyed, as before [M]. Not ported: the STORY_DESTROY_BASE event of step 3, notification 0x26,
  step 6's list, and the structure-radius pass in 0x4bb4a0 (type +0x1a00).
