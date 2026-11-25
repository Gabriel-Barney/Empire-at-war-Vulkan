# Single-player dialogs retail opens

Retail FoC x64 `StarWarsG.exe`, read in the Ghidra export and the disassembly (#423, 2026-11-24). Tiers: **[H]** read
directly, **[M]** inferred with good support, **[L]** guess. Addresses are image offsets from 0x140000000.

The GUI opens a dialog by name: `0x1c1570(&GUI 0x140a78510, "IDD_...", handler, ...)` returns the dialog's handle, and
`0x1c1d20(&GUI, handle, 1)` closes it. Every opener passes a literal name; no dialog is opened by resource id and no
format string builds a name (`IDD_%`, `%s_DIALOG`: no hits) [H]. So a dialog whose name is not in the exe can only be
opened by data, and no Lua or XML in the shipped data opens a dialog by name [H].

| Dialog | Verdict | Where, trigger, mode | Tier |
|---|---|---|---|
| IDD_TUTORIAL_DIALOG | **opened** | story reward `TUTORIAL_DIALOG` (type 0x17), galactic or tactical | [H] |
| IDD_MAIN_CAMPAIGN_DIALOG | **opened** | front end: Single Player > New Campaign | [H] |
| IDD_REINFORCEMENT_DIALOG | never opened | its opener is reached only from callbacks nothing calls | [H] |
| IDD_INVADE_PLANET_DIALOG | never opened | its opener is a command-bar callback nothing calls | [H] |
| IDD_LOAD_MAP_DIALOG | never opened in a retail session | only from a debug button that is switched off, or a dialog that is never opened | [H] code, [M] that a switched-off button cannot be clicked |
| IDD_INVASION_AUTO_RESOLVE_DIALOG | never opened | the name is not in the exe | [H] |
| IDD_PLANET_QUICK_REFERENCE | never opened | the name is not in the exe | [H] |
| IDD_PLANET_MAINTENANCE_DIALOG | never opened | the name is not in the exe | [H] |
| IDD_PRODUCTION_MENU_DIALOG | never opened | the name is not in the exe | [H] |

## The four names that are not in the exe [H]

- **What was searched:**
  - exact ASCII and UTF-16 strings;
  - 8-byte and 4-byte immediate chunks of each name in `.text` (`IDD_INVA`, `SION_AUT`, `O_RESOLV`, `ET_QUICK`,
    `_REFEREN`, `ET_MAINT`, `ENANCE_D`, `IDD_PROD`, `UCTION_M`, `ENU_DIAL`). All have zero hits.
- **In the shipped data** the names appear only in:
  - the RESOURCE.H defines (108 PRODUCTION_MENU, 109 PLANET_MAINTENANCE, 110 INVASION_AUTO_RESOLVE,
    156 PLANET_QUICK_REFERENCE);
  - the GUIDIALOGS.RC templates;
  - for PLANET_QUICK_REFERENCE, a text-style block in GUIDIALOGS.XML.

## IDD_REINFORCEMENT_DIALOG: never opened [H]

- **The opener** is `0xdbab0`. It lists the local player's units in `IDC_INVADING_FORCES_LIST`.
- **How it would be reached:** only by tail jumps from `0x36250` and `0x37760`, two identical "buy / reinforce" callbacks.
  - In a skirmish battle they open `IDD_SKIRMISH_BUY_UNITS_DIALOG`.
  - In a campaign battle with no retreat running they open the reinforcement dialog.
- **Why neither runs:**
  - `0x36250` has no reference.
  - `0x37760` is only passed to `TheCommandBar.System_Initialize` `0x30ae50`, which stores it at command bar + 0x2818.
  - No code calls that slot. A scan of `.text` for the displacement 0x2818 finds only the store, the re-init copy
    `0x2ea272` and fields of other classes. Compare the slots that are called: +0x2810 at `0x2fde83`, +0x27c8 at
    `0x3e40f9`.
- **Also settled:** the in-battle skirmish buy-units path is dead too (the legacy SKIRMISH_SETUP §2.7 question).

## IDD_INVADE_PLANET_DIALOG: never opened [H]

The opener `0xa7aa0` (arguments fleet and planet, `TEXT_INVADE_PLANET`) is likewise only passed to `0x30ae50` and stored
at command bar + 0x27d8, which nothing calls: the scan finds only the store and the re-init copy `0x2ea2d2`. It is not
the story reward `INVADE_PLANET` (0x22, `0x45f5c0`), which the port implements.

## IDD_LOAD_MAP_DIALOG: never opened in a retail session

The opener `0xbf9b0` has two outside callers:

- **The single-player menu's `IDC_DEBUG_LOAD_SKIRMISH_MAP_BUTTON`.**
  - The caller is in the `IDD_SINGLE_PLAYER_GAMES_MENU_DIALOG` handler `0xe9920`.
  - The menu's open `0xe9450` switches that button off unconditionally (`0x1c30d0(..., 0)`), and nothing turns it on [H].
  - That a switched-off button cannot be clicked is [M].
- **`IDC_GROUND_COMBAT_BUTTON` of `IDD_SKIRMISH_OPTIONS_DIALOG`.** That dialog's opener `0xf0e90` has no caller, so the
  dialog is never opened [H].

## IDD_TUTORIAL_DIALOG: opened [H]

- **The opener** is `0x123d70`. It is a no-op while the dialog is open.
- **What it shows:**
  - `IDC_TUTORIAL_TEXT` from the reward's text id;
  - `IDC_CONTINUE` only when the reward asks for it.
- **The trigger chain:**
  - `TheTutorial.Init` `0x447190` stores the opener in the tutorial object.
  - The only caller of that slot is `0x446c70`.
  - `0x446c70` is called only by the story-reward executor `0x456970`, case 0x17 (`TUTORIAL_DIALOG`).
  - The arguments are `Reward_Param1` (the text id) and `atoi(Reward_Param2) != 0` (the Continue button).
- **Quit:** the handler asks `TEXT_QUIT_SURE` (callback `0x124730`). Yes quits; No or Esc reopens the dialog with the
  same text and Continue flag.

## IDD_MAIN_CAMPAIGN_DIALOG: opened [H], front end

- **The trigger:** the single-player menu handler `0xe9920`, on `IDC_NEW_CAMPAIGN_BUTTON`.
  - When the profile's `TutorialsPlayed` is not empty, it closes the menu and opens the dialog.
  - Otherwise it writes "-1" to `TutorialsPlayed`, saves the profile and asks `TEXT_SUGGEST_TUTORIAL` (Yes / No). Yes
    opens `IDD_SELECT_TUTORIAL_DIALOG`; No opens this dialog.
- **The opener** `0xc4d90`:
  - hides `IDC_DIALOG_CLOSE_BUTTON` and `IDC_LOAD_FACTION_A_PROGRESS`;
  - selects `IDC_RADIO_MEDIUM` (difficulty 1).
- **The handler** `0xc5250`:
  - EASY / MEDIUM / HARD set the difficulty to 0 / 1 / 2.
  - `IDC_MAIN_CAMPAIGN_START` or `IDC_PLAY_FACTION_A_BUTTON_BIG` looks up the campaign `Full_Story_Campaign_Underworld`.
    It hides Back and Start, shows the progress bar, then starts the campaign with the chosen difficulty.
  - `IDC_CANCEL_BUTTON` or Esc goes back to the single-player menu.
- **The exe has no `IDD_CAMPAIGN_SELECTION_DIALOG` string at all.** Retail FoC never opens the story-set list.

## Port

- **TUTORIAL** matches: `ui::GuiInfoScreens::show_tutorial`, the story reward in story_present.cpp, and the quit
  question.
- **MAIN_CAMPAIGN matches (#452):** `ui::FrontEnd::new_campaign` and the dialog's fill and handler in
  engine/ui/src/gui_menus.cpp.
  - The host keeps `TutorialsPlayed` as the options file's `tutorials_played` key. The question box is
    `IDD_DIALOG_BOX_QUIT_CONFIRM`. Esc on it closes it [port].
  - Data without `Full_Story_Campaign_Underworld` (EaW, mods) keeps `IDD_CAMPAIGN_SELECTION_DIALOG` (docs/DECISIONS.md
    "Front-end and skirmish choices").
  - The port does not write the played tutorials to `TutorialsPlayed`: it never needs more than "not empty".
- **The other seven** have no port version, which is correct.
