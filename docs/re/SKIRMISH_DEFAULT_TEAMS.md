# Skirmish staging: the default team of each slot (#435)

Retail FoC x64 `StarWarsG.exe` (v1.121.859328), read in the Ghidra export and the disassembly (reference only, not
copied) on 2026-11-26. Tiers: **[H]** read directly in the code, **[M]** inferred from the code with a gap named, **[L]**
a guess. Follows legacy docs/re/SKIRMISH_SETUP.md §2.8, which read the AI slot's team only. #435 asked for a retail
screenshot. The code below answers the question without one; a screenshot would be a check of this reading, not
needed evidence.

## The slot records [H]

The single-player staging screen keeps one record per filled slot: an array of 0x1d8-byte records at 0x140a11eb0 with
its count at 0x140a11eb8. The refresh 0xfe7c0 (through 0x1023d0 for each record) shows a record's fields in its slot's
combos:

| Record field | Combo |
|---|---|
| +0x90 | `IDC_PLAYER_COLOR_n` |
| +0x94 | `IDC_PLAYER_FACTION_n` |
| +0x98 | `IDC_PLAYER_TEAM_n` (row index, shown as `TEXT_MP_TEAM_NUMBER` + number) |

The team combo is refreshed from +0x98 whenever the game type (0x140a11f8c, the `IDC_GAME_TYPE_LIST` value) is not 1
(campaign). The skirmish types (Land 2, Space 3, Control 4) all show it.

## Where each slot's team comes from [H]

- **The record constructor 0x2c1f0** writes 0 to +0x90 and +0x94 (one 8-byte store) and to +0x98. A new record
  starts on team row 0.
- **The staging setup 0xf1230** builds two records:
  - the local player: the profile's faction (0x140a7c29c) and colour (0x140a7c298), and team +0x98 := 0. The value is
    written directly; it is not read from the profile;
  - the AI (`TEXT_AI_PLAYER_NAME`): faction local XOR 1, and team := local team XOR 1 = 1, or 0 when that is not below
    the team count (0x140b27668).
- **Opening a later slot.** The `IDC_PLAYER_NAME_n` handler in 0xf2690, rows 2 to 4 (Easy, Medium, Hard AI), does this:
  - when n is beyond the record count, it builds a fresh record (0x2c1f0). It sets the name (`TEXT_AI_EASY_PLAYER_NAME`
    and the others, plus a number), the colour +0x90 := n, the faction +0x94 := local XOR 1 (0 when unplayable), and
    +0xa0 := the record count. It appends the record and refreshes;
  - it never writes +0x98, so the slot opens on team row 0, the same team as the local player;
  - an existing record (the slot was filled before) is copied back with its team.
- **The team handler** (same function, `IDC_PLAYER_TEAM_n`) writes the picked row to +0x98, clamped to the map's team
  count minus 1 (0x140a62290). For slot 0, it also stores the row in the profile global 0x140a7c2a0. The setup does
  not read that global back.

## Result

With the team boxes untouched, the slots open on:

| Slot | 0 (local) | 1 (AI) | 2 to 7 (opened) |
|---|---|---|---|
| Team row | 0 | 1 (0 with one team) | 0 |
| Shown | Team 1 | Team 2 | Team 1 |

So opening slots 2 and up without touching the team boxes puts those AIs on the local player's team.

## Port rule

`FrontEnd::fill_skirmish_slots` opens slot 0 on row 0, slot 1 on row 1 and every later slot on row 0. A slot whose
combo already has a valid selection keeps it. The Start action's fallback for a combo with no selection uses the same
defaults. [port] The port's team list always has at least two rows (one per shown slot), so the AI's "0 with one team"
case does not arise.
