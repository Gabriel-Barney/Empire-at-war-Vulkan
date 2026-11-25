# Holocron summary sort keys

Retail FoC x64 `StarWarsG.exe`, read in the Ghidra export and the disassembly (#429, 2026-11-24). Tiers: **[H]** read
directly, **[M]** inferred with good support, **[L]** guess. Addresses are image offsets from 0x140000000.

## Rows and the sort [H]

- **Openers.** Economy `0x10f5a0`, Fleets `0x114660` and Production `0x117c80` each rebuild the rows. There is one row
  per planet the local player owns, in the game-object list order.
- **The row build `0x10a4a0` resets the sort** every time it runs. It clears key and direction, sorts by name A→Z, then
  sets key 1 with the "reverse next" flag.
  - So each open of a summary page, including a switch between the three, shows the rows A→Z.
  - The first PLANETARY click gives Z→A.
  - The legacy HUD_GAPS §10.2 "kept across opens" is wrong.
  - The 1 s refreshers `0x110430` / `0x1154a0` / `0x118b60` have no caller [M].
- **The sort.** `0x10a250(key)` sorts with comparator A on the first click of a key, and with B when the same key is
  clicked again; each click toggles.
  - The sort is MSVC `std::sort`.
  - Up to 32 rows it is an insertion sort, which is stable.
  - Comparators: a numeric key A is largest first; the name key A is A→Z by UTF-16 code unit. B is the reverse of A.

## Keys [H unless tagged]

| key | button | value |
|---|---|---|
| 1 | PLANETARY | the planet name |
| 2 | BASE_CREDIT_VALUE | base value |
| 3 | TRADE_ROUTE_INCOME | trade income |
| 4 | MINING_FACILITY_INCOME | the planet's mining income |
| 5 | TOTAL_INCOME | 2 + 3 + 4 |
| 6 | POP_CAP | the planet's population capacity summed with its structures' [M] |
| 7 | PRODUCING | 0..2: each build queue (space, ground) whose head type has an icon (`Icon_Name` [M]) counts 1 |
| 8 | UPGRADES | the number of the planet's persistent upgrade objects |
| 9 | LAND_FLEETS | distinct allied land types on the surface; no button in the FoC dialog |
| 10 | SPACE_FLEETS | distinct allied types over three orbit groups; no button in the FoC dialog |

**Key 7.** `0x10b0f0` reads the head of each planet build queue (`0x401960`). It copies the head type's +0x1820 string
into the row. A queue counts 1 only when it has a head and that string is non-empty.

**Key 8 and the Upgrades icons.**
- The count is the size of a 16-byte-record vector on the planet's data: `{type, owner, frame}`.
- `0x4b7090` adds a record when a DUMMY_UPGRADE completes in a campaign battle. `0x4bb690` removes one.
- The count has no owner filter.
- The Production row filler `0x11c020` puts the first 8 records' icons into `IDC_HERO<row>_ICON0..7`, under the
  TEXT_UPGRADES header.
- These icons are upgrades, not heroes. Follow-up #459.

## Port

- **Open order:** `ui::InfoScreens::show_info` resets the sort to key 1 A→Z on each summary open.
- **Key 7:** counts a queue only when its head type has an icon (`engine/apps/game/app/game_info.cpp`).
- **Key 8 and the Upgrades icons (#459):**
  - `galactic::PlanetState::upgrade_objects` holds `{type, owner, frame}`. The port writes the galactic frame into the
    third field [L].
  - The list changes through `GalacticSim::add_upgrade_object` / `remove_upgrade_object` (`0x4b7090` / `0x4bb690`; an
    add of the same owner and type is refused). It is saved (galactic save v13; older saves read none) and hashed only
    for planets with records.
  - The battle installs them through `TacticalBattle::set_upgrade_tracking`, behind the pads' campaign gate:
    - the add on a finished DUMMY_UPGRADE (`0x498170`);
    - the remove when a production upgrade object leaves with its container, or with a station upgrade (`0x379df0`).
  - Key 8 counts the list, and the 8 icons are its first 8 records, with no owner filter (`game_info.cpp`).
  - Tactical production runs in skirmish only in the port, so the list fills once campaign battles produce (#544).
- **Still `[port]`:** keys 6, 9 and 10 use the port's unit and fleet counts. Keys 9 and 10 cannot be reached in FoC.

## Open

- The third field of the upgrade record [L], and whether retail's save writes the list (the port's does).
- The orbit group of key 10 [L].
