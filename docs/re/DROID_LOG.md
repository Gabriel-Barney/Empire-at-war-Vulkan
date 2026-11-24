# Droid Log

Retail FoC x64 `StarWarsG.exe`, read in the Ghidra export and the disassembly (#428, 2026-11-24). This answers whether
the galactic Droid Log is saved, and confirms its row colours. Tiers: **[H]** read directly, **[M]** inferred with good
support, **[L]** guess. Addresses are image offsets from 0x140000000.

The port is `App::session_started` (engine/apps/game/app/app_session.cpp) for the clear and
engine/apps/game/app/app_story_present.cpp for the append.

## The rows are not saved [H]

- **Append** `0x9d2a0`. Its only caller is the advisor line push `0x49810` (at 0x49c76). Each line adds three rows:
  - `TEXT_LOG_CYCLE_01`, formatted with the galactic day (`*(int *)(galactic mode + 0x370)`), in `Droid_Date_Color`;
  - the line's text, in the line's own colour;
  - `TEXT_HELP_SEPERATOR`, in `Droid_Seperator_Color`.
- **Container.** A file-scope `std::vector` of 0x30-byte rows: a `std::wstring` at +0 and a colour at +0x20. Begin,
  end and capacity are at `0xa61e98`, `0xa61ea0` and `0xa61ea8`. It belongs to no game object.
- **Every reference to it:**
  - the append `0x9d2a0`;
  - the clear `0x9da90`;
  - the `IDC_DROID_LIST` fill `0x9db60` (display only);
  - the atexit destructor `0x7e84c0`.

  No pointer to the vector exists elsewhere in the image, and no save or load writer references it. The rows and their
  colours are therefore not written to a save and not read back on load.
- **Clear.** `0x9da90` has five call sites, all on front-end transitions:
  - closing the main menu (`0xc6440`, only while the main-menu flag is set);
  - opening `IDD_SINGLE_PLAYER_GAMES_MENU_DIALOG` (`0xe9450`);
  - opening `IDD_MULTIPLAYER_DIALOG` (`0xcd1c0`);
  - two `IDD_MP_CONNECT_DIALOG` paths (`0xc7ca0`).

  A new game, and a load started from the front end, pass one of them, so the log is per session.
- **A load started inside a running game keeps the previous rows [M].** The save-list load `0xbd960` closes the main
  menu, and so clears the log, only while the main menu is open. None of the other clear sites is on the in-game load
  path, although that path was not traced in full.

## Colours [H]

- The GameConstants static is at `0xb15800` (its constructor is `0x2c4370`, called at `0x173eb`).
- The Day row takes GameConstants + 0x12a0 (`Droid_Date_Color`), and the separator takes + 0x12c0
  (`Droid_Seperator_Color`). The offsets are as in engine/objects/src/field_schema_table.inc.
- Retail copies both colours at append time.

## Port

- The append matches: same keys, same colours (app_story_present.cpp).
- The rows are cleared at every session start, which matches retail for a new game and for a front-end load.
- [port] A load started inside a running game also starts with an empty log; retail keeps the old rows [M]. The
  difference is cosmetic and left as is.
