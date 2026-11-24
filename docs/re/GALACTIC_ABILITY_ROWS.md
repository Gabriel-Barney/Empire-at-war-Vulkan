# Galactic special-ability rows (g_special_ability)

Retail FoC x64 `StarWarsG.exe`, read in the Ghidra export and the disassembly (#437, 2026-11-24). Tiers: **[H]** read
directly, **[M]** inferred with good support, **[L]** guess. Addresses are image offsets from 0x140000000. Component ids
come from the HUD component name table at `0xa174e0` (72-byte entries: the name pointer at +0, the id at +8): 0x5f
`Slot_00`, 0x206 `g_special_ability`, 0x1f2 `g_planet_fleet`, 0x202 `g_planet_land_forces`.

## A correction to the legacy MCX4_T09 Q6

`0x3e8890`, the "hero selected alone" path, drives component **0x5f `Slot_00`**, the fleet organiser slot. It does not
drive `g_special_ability` [H].
- It shows `Slot_00` only when the grabbed organiser slot holds one hero.
- It classifies that unit at the selected planet and labels the slot.

`g_special_ability` is a different element: a per-planet drag-and-drop target, drawn by `0x3eb200` [H].

## The kind and its label [H]

`0x3d2020` takes the object with the lowest `Ranking_In_Category`. It returns -1 when the planet is story-blocked
(`0x4470b0`) or `0x326ab0` refuses; otherwise it tests the kinds in this order:

| kind | test | text key |
|---|---|---|
| 6 | `0x2e5c20` destroy planet | `TEXT_TOOLTIP_ABILITY_DESTROY_PLANET_NAME` |
| 1 | `0x2e6b20` slice | `TEXT_ABILITY_SLOT_SLICE` |
| 9 | `0x2e5740` black market | `TEXT_ABILITY_SLOT_BLACK_MARKET` |
| 2 | `0x2e6770` sabotage | `TEXT_ABILITY_SLOT_SABOTAGE` |
| 3 | `0x2e6d80` system spy | `TEXT_ABILITY_SLOT_SPY` |
| 4 | `0x2e62a0` neutralize hero | `TEXT_ABILITY_SLOT_NEUTRALIZE_HERO` |
| 5 | `0x2e68c0` siphon credits | `TEXT_ABILITY_SLOT_SIPHON_CREDITS` |
| 7 | `0x2e59b0` corrupt planet | `TEXT_ABILITY_SLOT_CORRUPT_PLANET` |
| 8 | `0x2e6500` remove corruption | `TEXT_ABILITY_SLOT_REMOVE_CORRUPTION` |
| 10 | `0x2e6110` hack super weapon | `TEXT_ABILITY_SLOT_HACK_SUPER_WEAPON` |
| -1 | none | none: the component is hidden (`0x3e8890` and `0x3eb200` both hide it) |

- **Raid:** `0x3eb200` adds kind 0, `TEXT_ABILITY_SLOT_RAID`, when `0x2e6020(fleet, target planet)` is true. That
  override also replaces -1 [H for the code, M for what `0x2e6020` tests].
- **Every kind has a key.** The label is looked up in the text database and set on the component. The unit's name is
  never used.

## The row [H, M where marked]

- **Count.** The planet record build `0x3d3790` clones the `g_special_ability` template twice per planet. `0x3eb200`
  only ever shows the first clone, so there is at most one row per planet.
- **Refresh.** `0x3ea1b0` refreshes every planet each update, so several planets can show their row at once.
- **When it shows.** While a `g_planet_fleet` or `g_planet_land_forces` button is dragged (the HUD's grabbed component
  at +0x2160 [M]), every planet the move is valid to (`0x3f0010`, not read) shows its row:
  - the dragged group is classified against that target planet;
  - the kind is forced to -1 unless the group holds exactly one object or the kind is 6;
  - then the raid override applies.
- **Use.** A drop of the dragged button on the row uses the ability: release handler `0x2f6c30`, then `0x3d2900`.
- **Story-forced planets.** A set of story-forced planets (`0xb30578..0xb305b0`) always shows a fixed kind. Who writes
  that set was not traced.
- **Plain selection.** Selecting a hero on the map shows no `g_special_ability` row.
- **Enabled [M].** Nothing disables a row: `0x3eb200` and `0x3d3790` never call the disable method, and the template
  has no `Disabled` entry.

## Port

- **Labels:** `activated_ability_slot_of` (engine/galactic/src/special_systems.cpp) now gives every kind its key. It
  used to give "" for 9, 2, 7 and 10, which were untraced.
- **Rows:** the g_special_ability provider (engine/apps/game/app/app_hud.cpp) gives a unit with kind -1 no row. It
  used to show the unit's name.
- **Enabled:** rows are always enabled, which matches retail.
- **[port]** The rows appear beside the selected planet, one per ability unit there, and are used with a click.
  Retail shows one row per reachable planet during a fleet or land-forces drag and uses it with a drop. That change,
  with its cap of one row per planet, is follow-up #455. Until then, a single row would hide every hero's ability
  but the first.
