# Gameplay options and the mouse tables

Retail FoC x64 `StarWarsG.exe`, read in the Ghidra export and the disassembly (#439, 2026-11-24). Tiers: **[H]** read
directly, **[M]** inferred with good support, **[L]** guess. Addresses are image offsets from 0x140000000.

Follow-ups: t09 #467, t10 #468, t11 #469, autofire default #473.

Options dialog handler = `0xa0f30` (OK branch reads the checks into the profile record `0x140a7c230`; Defaults branch
`0xa1a3d..`, fill `0x9f320` / `0x9f7ab`). Profile ctor `0x202510`. Captions: GUIDIALOGS.RC (Patch2.meg) + MasterTextFile.

| IDC | profile byte | ctor default | Defaults button | caption |
|---|---|---|---|---|
| IDC_ALTERNATE_MOUSE_CONTROLS | 0x140a7c46d (+0x23d) | 0 | 0 | TEXT_ALTERNATE_MOUSE_CONTROLS "Alternate Mouse Controls" |
| IDC_AUTORESOLVE | 0x140a7c46f (+0x23f) | 0 (dword +0x23c = 1) | 0 (`0xa1a63 xor r9d`) | TEXT_ASK_BUTTON_AUTO_RESOLVE = **"Auto-Resolve"** |
| IDC_SIMPLIFIED_FACTION_COLORS | 0x140a7c475 (+0x245) | 0 (word +0x244 = 0) | 0 (`0xa1cc5`) | "Simplified Faction Colors" |
| (side) IDC_ABILITIES_DEFAULT_TO_AUTOFIRE | 0x140a7c476 (+0x246) | **1** | **1** | - |

## t09 IDC_AUTORESOLVE: "always auto-resolve my battles", not a confirmation [H]

- OK handler stores the check at `0xa13d1` -> 0x140a7c46f. Readers (complete xref list): dialog fill `0x9f560`, and the two
  galactic conflict deciders `0x2b9ab6` (FUN_1402b92e0) and `0x2b8222` (FUN_1402b76a0, same code). No other reader.
- At `0x2b9aa4..0x2b9ad6` (single-player branch, `FUN_14028af60(&0x140b153e0)` false = not a multiplayer vote [M]):
  `if (!local_player_is_attacker_or_defender || overrun(+0x635) || profile_0x140a7c46f) { state +0x618 = 6; +0x634 = 1;
  timer reset }`. State 6 -> 7 is the auto-resolve sim (0x140b31400: add both sides' units, run vf+0x30 after the
  Space/Land_Auto_Resolve_Delay). So with the box checked, every single-player conflict involving the local player is
  auto-resolved at once, exactly like AI-vs-AI and overrun conflicts, and the battle-pending prompt (`0x2bce30`) is never
  shown. Unchecked (default) -> the normal prompt.
- The test does not consult `bVar24` (story lock `0x4472a0` / game option "never"): the overrun term is gated by
  `!bVar24`, the profile term is not [H, code read at 0x2b9ab6; whether some earlier path keeps story-locked battles out
  of this state is not traced - see "open"]. It is not read in the multiplayer branch.
- The tactical IDC_AUTORESOLVE (retreat dialog, `0xdc5d0`) is a different control with the same name; unrelated.

**Port (#467, landed):** `GameplaySettings::auto_resolve = false`, options key `auto_resolve` (the old `ask_autoresolve`
key is not read); `ConflictConfig::always_auto_resolve`, read in Decide as below and pushed by
`App::apply_gameplay_options` (also at every session start). The story lock stays a guard [port] (see "open").

**Change [H]:**
1. `GameplaySettings::ask_autoresolve` -> `auto_resolve = false` (gui_menus.hpp:301, gui_menus.cpp:1466/1490); options key
   `auto_resolve` (options_pages.cpp:31; do not map the old `ask_autoresolve` key - every existing port profile has it =1).
2. `ConflictConfig`: `bool always_auto_resolve = false;  // profile 0x140a7c46f, 0x2b9ab6`. In Decide, after the overrun
   block: `const bool local_in = local != kNoPlayer && (head.attacker == local || head.defender == local);`
   `if ((!hum && !cfg_.fight_ai_battles) || overrun_loser != kNoPlayer || (local_in && cfg_.always_auto_resolve))` ->
   force_ = true, retreating_ = overrun_loser, AutoSetup (retail sets +0x634 = 1, +0x638 stays -1). Not story-gated.
3. `App::apply_gameplay_options` (app_options.cpp:56) pushes `gameplay_.auto_resolve` into the session's ConflictConfig
   (and at session start). The existing auto-resolve result path (auto_resolve_result, app_screens.cpp:597) shows it.
- Test (engine/galactic tests): a human-vs-AI space conflict under the overrun multiple; `always_auto_resolve = true` ->
  after one service the state is AutoSetup/AutoRunning and the `decide` callback was never called; `false` -> `decide`
  called (Vote). Plus test_game_display: default `auto_resolve == false`, round-trip of the key.

## t10 IDC_SIMPLIFIED_FACTION_COLORS [H]

Readers of 0x140a7c475: fill `0x9f855`, OK `0xa166c`, and three game readers:
1. **`0x33e340` faction colour lookup** `(FactionClass f, bool raw)`, ~90 call sites (unit colorization `0x396190`, HUD,
   radar, galactic markers, tooltips...). If a local player exists and (`local_player+0x3c0` (tutorial player, set by
   story reward SET_TUTORIAL_PLAYER `0x456970`) || (option && !raw)): f == local player's faction -> f+0xe8; else if
   !f.Is_Neutral(+0x108) && f.Is_Playable(+0x20a8) -> f+0xf8; else f.Color(+0xa8). Faction ctor `0x33bf5c/0x33bf83`
   copies GameConstants **Player_Color** (GC+0x334, FoC `50, 209, 63, 255`) to +0xe8 and **Enemy_Color** (GC+0x344,
   `239, 9, 9, 255`) to +0xf8 (GameConstants parse table 0xa33e50/0xa33e70). It compares factions, not players: any
   other playable faction (an ally too) gets Enemy_Color. Only `raw = 1` callers: the FoC corruption text colour in the
   planet tooltip (`0x2e2910` / `0x2ed780`), so those keep the Underworld colour. Unit team colour `0x396190` takes this
   path only outside the MP-colour mode (`FUN_14028afb0` false; MP uses the player's MP colour) [H].
2. **`0x3ec380` planet hero medals**: option on -> `g_hero` "you" (slot +0xf0, hero allied to the local player,
   `0x39a230`) and `g_enemy_hero` (slot +0xf8, other hero passing `0x2e6810`) shown, the three faction medals
   (rebel/empire/underworld, slots +0x100..+0x110) hidden; option off -> the reverse (`0x3ec971..`).
3. **`0x5c28e0` trade-route class**: same-owner, non-hostile, non-neutral ends: option on -> 1 (Ally) if the owner is not
   hostile to the local player (`0x2823e0`), else 0 (Enemy); off -> Good / Evil / Corrupt by faction name. This is the
   global the port calls "multiplayer".

**Port today:** nothing reads `simplified_colors` (gui_menus.hpp:302). `render::classify_route(..., bool multiplayer)`
(`engine/render/galactic/include/alamo/render/galactic/galactic_view.hpp:183-190, 227`) is the 0x5c28e0 rule but the flag is
mislabelled and pinned false (`engine/apps/game/app/game_render.cpp:200`). Medals: `galactic_hud.hpp:127` says "never drawn".
Colours: `make_galactic_palette` `engine/render/galactic/src/galactic_snapshot.cpp:139-160`, `App::faction_rgba`
(`engine/apps/game/app/app_hud.cpp:33`, text=false path), tactical `SnapshotTypeTable::team_color` (`sim_snapshot.cpp:283`).

**Change [H]:**
- Rename `GalacticViewOptions::multiplayer` -> `simplified_colors` (comment: IDC_SIMPLIFIED_FACTION_COLORS 0x140a7c475,
  0x5c28e0) and set it from `gameplay_.simplified_colors` at game_render.cpp:200.
- One pure helper, e.g. in galactic_snapshot.hpp: `materials::float4 display_faction_color(const FactionColor& f, bool
  local_faction, bool is_playable, bool is_neutral, bool simplified, float4 player_color, float4 enemy_color)` = the
  0x33e340 rule; Player_Color / Enemy_Color read from GameConstants (defaults 50,209,63 / 239,9,9). Use it in
  `make_galactic_palette` (new args: local player's faction, simplified), `faction_rgba(text=false)`, and the tactical
  single-player team colour. `Display_Font_Color` (text=true) is read directly in retail, not through 0x33e340: unchanged.
- Medals: app_hud.cpp:108-115 fills `hero_you` / `enemy_hero` and draws them when simplified, faction medals otherwise.
- Tests: `classify_route(p, {0,1}, 0, neutral, sides, /*simplified*/true) == Ally` where sides would give Evil;
  palette test: simplified on -> local faction = (50,209,63)/255, another playable faction (also an ally) = (239,9,9)/255,
  a neutral faction keeps No_Colorization_Color; off -> faction <Color>.

## t11 the mouse tables [H]

Handler `0x30fc0` (tactical and galactic). Raw events: 0/1/6 left down/up/double, 2/3/7 right, 4/5/8 middle, 9/10
wheel, 11 move. `+0x120 == 1` -> default table `0x9cf2a0`; `+0x121 == 1` -> alternate `0x9cf230` (`0x313b6`):
default 0->9 1->10 6->11 2->12 3->13 7->14 4->15 5->16 8->17; alternate 0->0 1->1 6->2 2->3 3->4 7->5 4->6 5->7 8->8;
both 9->18 10->19 11->20. Jump table `0x333ec`. **Selection of the table:** `0x36d60(&0x140a60df0, profile 0x140a7c46d)`
at startup (`0x5e28a` in 0x5d990, unless 0x140a61a9e) and on Options OK (`0xa132e`); quirk: it switches to the alternate
only while the default flag is set. Cursor action codes are `0x39980`'s, executed by `0x41f70` (1/2 select, 4 move to
ground, 5 attack, 10/11 open the sell / special-ability menu of the object).

| button | default (actions) | alternate (actions) |
|---|---|---|
| L down | 9 `0x31952`: box-select anchor | 0 `0x31906`: box anchor (+ drag anchor +0x110) |
| L up | 10 `0x31f4b`: box select, or click: 10/11 or 4-on-object -> select; ground -> **clear selection** (`0x32259` -> 0x3af30) | 1 `0x31a83`: box select, or click: own/allied selectable -> select; else **execute the cursor action** (move 4, attack 5, ...); never clears |
| L dbl | 11: select all of type | 2 `0x32aaa`: select all of type; cursor 4 -> double-click move `0x3b360`, 5/0x22 -> double attack `0x3c220` |
| R down | 12: order anchor; with Right_Button_Scroll a drag past 0.08 pans | 3 `0x319b9`: **camera mouse drag starts at once** (`0x390d0`, the middle-drag of the default; no 0.08 threshold, `0x14` move: `+0x121` bypasses it) |
| R up | 13 `0x32368`: execute the cursor action (orders), drag -> facing move | 4 `0x326eb`: cursor 10/11 -> sell / ability menu; then **only if the cursor did not move since the press** (pos == +0x100) or galactic: cancel an armed mode (0x140b29fc1/fc2) else **clear selection** (`0x3296c` -> 0x3af30); moved -> end of the pan |
| R dbl | 14: double-click move / attack | 5: nothing |
| M down | 15: pan drag (Ctrl rotate) | 6 `0x32a62`: flag only, no drag |
| M up | 16: camera reset if not moved and no Ctrl | 7 `0x32a70`: **camera reset always** (`0x2cf800(cam,0)`) |
| M dbl | 17: nothing | 8: nothing |

So the port's guess is right on the left button and on "right click clears", with three differences: the right
button pans the camera in the alternate scheme (and only an unmoved right click clears / cancels), the middle button
does not pan and always resets, and the table applies in galactic too (left click = the context order, e.g. fleet move).

**Port today:** `engine/ui/src/orders.cpp:127-148` (alternate: left = select own / order; `rb.click` -> `Select{}`),
`engine/ui/include/alamo/ui/orders.hpp:161-166` ("tactical only", "actions 3..5 not recovered"); the camera ignores the
scheme: `TacticalCameraController::mouse` `engine/ui/src/camera.cpp:562-597`, tuning from `App::option_tuning`
`engine/apps/game/app/app_input.cpp:1578-1585`.

**Change [H]:** `CameraTuning::alternate_mouse` (set in option_tuning from `gameplay_.alternate_mouse`); in `mouse()`:
when set, the pan/rotate source is `rb` from its first moved pixel (no `right_acc_` threshold, Ctrl -> rotate), `mb.down`
does not drag, and `mb.released` calls `reset()` unconditionally; same for the galactic camera's drag-scroll. In
orders.cpp:145 the alternate right release clears only when `rb.drag` has zero extent (x0 == x1 && y0 == y1; a moved
press is a pan), and an armed move/attack toggle is cancelled instead of clearing. Update the orders.hpp comment with
the table. Tests: camera - alternate on, right drag 40 px -> look-at moved, middle drag 40 px -> not moved, middle
release after a drag -> pose == reset pose; orders - alternate on, right press+release with 10 px motion -> no
`Select{}`, without motion -> `Select{}`.

## Open (needs-info)
- t09: whether story-locked / AlwaysBypassAutoResolve conflicts ever reach 0x2b9ab6 with the box checked (the code there
  ignores the lock); a playtest with a campaign-locked battle and the box checked settles it. The port could keep the
  story lock as a guard [port] until then.
- t11: the galactic effect of each alternate action is read from the shared code only [M]; 0x140b29fc1/fc2 (what the
  right click cancels) not named.
- Side finding (not t09-t11): retail IDC_ABILITIES_DEFAULT_TO_AUTOFIRE defaults ON (ctor +0x246 = 1, Defaults passes 1).
  The port now defaults it on too, under the new key `abilities_default_to_autofire` (#473).
