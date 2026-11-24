# Campaign score dialog, fleet tooltips and the game options record

Retail FoC x64 `StarWarsG.exe`, read in the Ghidra export and the disassembly (#439, 2026-11-24). Tiers: **[H]** read
directly, **[M]** inferred with good support, **[L]** guess. Addresses are image offsets from 0x140000000.

Follow-ups: t12 #470, t13 #471, t14 #472.

## t12 Campaign score dialog (IDD_CAMPAIGN_SCORE_DIALOG, FUN_140092bb0)

**IDC_VICTORY_LIST is not a kill list. [H]** 0x92bb0 builds no rows from Get_Tactical_Killed_List or any kill table. It writes one
paragraph: the local player's faction `Victory_Text` (faction +0x2e10) when the winner's player +0x4c equals the local
player's, else `Defeat_Text` (+0x2e30). The text id is localised with 0x1fa680(...,1). FUN_140094030 then fills the list:
- 0x1c40b0 clears the list;
- 0x1c3f50 -> 0x1cd540 word-wraps the paragraph to the list width (control w - 4 x margin);
- 0x1c3c50 adds each line as a row, colour = colour table 0x140a7bca0 index 1 (COLOR_WHITE in the name table 0x140a13c08;
  [M] that the two tables share their order);
- 0x1c4920 / 0x1c4140 scroll the list to the top.
Offsets: field table 0xa3c910 / 0xa3c930. FACTIONS.XML: TEXT_REBEL_VICTORY / _DEFEAT, TEXT_EMPIRE_*, TEXT_UNDERWORLD_*.
The RC gives the list LBS_SORT. The port's gadget ignores sort styles (UI.md 3.7.1 [M]); with one wrapped paragraph that is the
only safe reading.

**IDC_BATTLE_VICTORY_IMAGE [H]:** on a win, faction +0x2e50 `Space_Win_Image` or +0x2e90 `Land_Win_Image`. On a loss, +0x2e70
`Space_Lose_Image` or +0x2eb0 `Land_Lose_Image`. The pick is 0x1ffb40(&0x140a13e20, 0, 1), an LCG on the unsynced UI random:
**0 picks Space, 1 picks Land.** Set by 0x1c39f0(..., white 1,1,1,1, 2, 1), then 0x1c3870. There is no fallback when the field is
empty; every playable faction has all four images.

**Efficiencies (aside) [H]:** text = L"%d" (0x7ffa20) of `(int)(stat*100.0)` clamped to 0..100, so it is truncated and has
**no "%"**. The missing-stat fallback is L"0%" (0x805590). The RC has no separate "%" static.

**Port today:**
- app_screens.cpp:942-959 already puts the faction text in `score.list` and the image in `score.image`
  (`game::game_over_art`, hud_feeds.cpp:300). The issue's "list empty" is stale.
- gui_screens.cpp:321 uses `d->set_items`: no wrap, skin colour.
- app_screens.cpp:948 passes `pick_land = rng.range(0,1) == 0` (0 picks Land).
- gui_screens.cpp:316 `pct` appends "%", and game_info.cpp:524 rounds with `lround`.

**Port change ([H]):**
1. gui_screens.cpp:321 -> `gui_->set_paragraphs(*d, "IDC_VICTORY_LIST", info.list, Rgba8{255,255,255,255});` (wrap, white,
   top).
2. app_screens.cpp:948 -> `cine_.rng.range(0, 1) != 0` as `pick_land` (cosmetic; it only changes which value of the draw picks
   which image).
3. Efficiency text: gui_screens.cpp:316 `pct` -> `num32(v)` with no "%"; a missing stat gives U"0%". game_info.cpp:524 ->
   `std::clamp(static_cast<int32_t>(e * 100.0), 0, 100)`.

Tests:
- test_gui_screens.cpp:62: a 300-character paragraph gives `items.size() > 1`, `top == 0` and white `item_text_colors`.
- military 0.876 shows "87".

## t13 Fleet and land tooltips

Dispatch is in the galactic tooltip builder 0x2e0c20 [H]:
- **Fleet stack (component 0x1f2):** after the open check 0x305fd0, 0x2efb40(planet, slot, comp) builds the tooltip, then
  `fleet_tooltip` is raised (0x32edd0 on mode+0x14). It is raised whether or not anything was drawn.
- **Land forces button (component 0x202):** 0x2f25a0(planet, comp), then `land_tooltip` is raised the same way.
- **A fleet object on the map** (0x2e2910 -> obj+0x336 != -1): 0x2f1130 builds the tooltip **only when the fleet's owner (+0x58)
  is the local player**; otherwise it returns 0, with no pop-up and no `fleet_tooltip` [H].

**Summary header 0x2eb070(owner obj, space?, total, comp, raid, stealth) [H]:**
- Title, chosen from the owner faction (tooltip +0x2708 / +0x2728 / +0x2748 = Rebel / Empire / Underworld [M, the same
  comparisons as the emblem index 0x3d18f0]): TEXT_GOOD_ / TEXT_EVIL_ / TEXT_CORRUPT_ / TEXT_PIRATE_ (+ `SPACE_FLEET`, or
  `LAND_GARRISON` for land).
- Number: `total` = sum over groups of pop(T, mode) x count. pop = 0x373500: Population_Value (+0x2124), or the
  Space_ / Land_Override (+0x2128 / +0x212c) when the mode vf 0xe0 returns 2 / 1, so in galactic it is the base value
  [M on the galactic mode id].
- The number is passed as **-total** to 0x2eb890, which draws e_topbar.tga with the number as "%d" in gold (255,215,56); a
  positive value takes another colour ([M]: 0x1f9470 sets it).
- Icon: the hovered component's own texture and frame (vf 0xf8 / 0x100), i.e. the stack art. Without a component it is the
  representative icon plus stack frame ceil(n / 0x140b16b98) of component 0x1f2.
- Tint: the owner faction colour (0x33e340(faction, 0)).
- Badges: component 0x274 frame 0 when stealth, frame 1 when raid.

**Fleet slot 0x2efb40 [H]:**
- Fleets in the slot (0x3f9370) are counted when the local player owns them (0x39a230), when they are fully visible
  (0x3d7400), or when they are only detected (0x3d7390, which marks the info partial).
- Each counted fleet's direct members are grouped by type key 0x2f6270 (0x3cac60, at most 0x80 groups) into
  (type, representative, count).
- After the header, a centred text line: TEXT_RAID_FLEET when every fleet is a raid fleet (0x432e20), TEXT_STEALTH_FLEET
  when every fleet is stealthed (0x3a9760), both joined with ", " (0x803074).
- Then one cell per group (0x3095c0):
  - icon = type `Icon_Name` (+0x1820); "i_button_unknown.tga" when not known; "i_button_temporary.tga" when there is no
    icon;
  - overlay "i_icon_stealth.tga" when the unit is stealthed;
  - label "x%d" (0x804eb0) when count >= 2;
  - six cells per row, the first offset by half a cell [M geometry].

**Land 0x2f25a0 [H]:**
- Only when the surface is fully visible (0x50b970: known icons) or partly visible (0x50ba40: unknown icons); otherwise no
  pop-up.
- Groups the planet's surface objects (0x397120) that belong to the planet owner (0x39a2c0), keyed by type.
- No groups -> no pop-up.
- Header: `LAND_GARRISON` title, no badges, the same cell grid.

The faction-emblem index 0x3d18f0 (with Galactic_Fleet_Override_Icon_Index) is used in the **planet** branch of 0x2e2910,
not in the fleet summary. This corrects GALAXY_MAP_PRESENTATION §1.4 [H].

**Port today:**
- app_screens.cpp:1056-1081: a fleet stack shows the encyclopedia pop-up of its highest-Tactical_Health unit.
- A land stack shows nothing (type stays kNone).
- `fleet_tooltip` / `land_tooltip` fire once per hovered stack. A map fleet of any owner fires `fleet_tooltip`
  (app_screens.cpp:1040).

**Port change ([H] content, [M] layout):**
1. Add a pure builder to hud_feeds:
   `FleetTooltip fleet_tooltip_summary(owner_faction, land, groups{type, count, pop, known, stealth}, all_raid, all_stealth)`
   -> {title key, -total, badge keys, cells{icon, "x%d" or "", unknown, stealth}}.
2. Give `EncyclopediaEntry` a cell grid (`std::vector<{icon, label, unknown, stealth}>`, six per row) and an owner tint.
3. In app_screens.cpp:1056, build the entry for fleet and land stacks instead of the representative unit's.
4. Gate the map-fleet hover (line 1040) on owner == local player.

Unit test (test_game_session or a hud_feeds test): an Empire fleet with 3 x TIE (pop 1) and 1 x Star Destroyer (pop 5) gives
TEXT_EVIL_SPACE_FLEET, number 8, cells [TIE "x3", SD ""]. A land garrison gives TEXT_*_LAND_GARRISON. An all-raid fleet gives
the badge TEXT_RAID_FLEET.

## t14 Game options record 0x140b1549c (GMM+0xbc, 0x24 bytes)

Layout as legacy docs/re/SKIRMISH_SETUP.md: option at +0xc = 0x140b154a8, flags at +0x20 = 0x140b154bc.

**Boot defaults [H]:** 0x4acc60 sets option 2 and flags 0x1f (byte 0x140a2ab88). The 0x20 bit ("an option was changed") is
clear, so by default the option is never consulted.

**Readers in single player [H]:** found through every rip-relative reference, and a scan for `[reg+0xdc],0x10` and
`[reg+0xc8],1/3` that found no GMM-relative use.
- The option is read only by the battle-pending state machines 0x2b92e0 (space) and 0x2b76a0 (land), and only when flag 0x20
  is set.
- Flag 0x10 (random events) has exactly one reader: Game.InterventionsEnabled 0x6e1fb0 (flag 0x20 set -> 0x10 ? 1 : 0). It gates
  no story plot or random event, only the AI's Interventions equation. The port matches: session_spec.hpp:82,
  gui_menus.cpp:965-983.

**Option 1 "never" [H]:**
- It sets the "no auto-resolve" boolean that the story lock also sets (0x2b9a11 path). Its only effect is to **skip the overrun
  check**.
- It then takes the same branch as "ask": force lists (0x2bce30) and `PENDING_TACTICAL_BATTLE_VOTE`.
- The banner's button set 0x3ca480 does not read the option. It is one-button only for GC+0x2f0, the linked flag 0x140b30545,
  the tutorial / story unit lock, player+0x3c0 or the story lock 0x4472a0.
- So retail "never" shows **Begin and Auto-Resolve**.
- In FoC the overrun check is inert: the GameConstants init 0x31b590 (decomp line 542033) sets +0x1588 = 0, and
  `Overrun_Auto_Resolve_Multiple` is commented out in GAMECONSTANTS.XML. So "never" behaves exactly like "ask".

**Option 3 "overwhelming odds" [H]:**
- The overrun check runs (inert, as above).
- If it does not auto-resolve, and the battle is not story-locked: state 5 and GUI hook 0x140b15638 = **0x86550**
  (registered at 0x86ff0), called with kind 2 = space / 1 = land. The force lists are built too.
- 0x86550 opens **IDD_MP_VOTE_DIALOG**: IDC_CAPTION `TEXT_ASK_AUTO_RESOLVE`, LEFT `TEXT_ASK_BUTTON_TACTICAL` (vote 1),
  RIGHT `TEXT_ASK_BUTTON_AUTO_RESOLVE` (vote 2), callback 0x86d40.
- On the wall clock, after `AutoResolveVoteDefaultTimeOut` (GC+0x2f4 = 15 s) the default vote is cast: auto-resolve unless
  `AutoResolveVoteDefaultToTactical` (False).
- Result: tactical starts the battle (0x8d930); auto-resolve runs 0x83c10.
- Hook 0x87700 (0x140b15640) only blocks a second dialog while one is open (byte 0x140a61c38).
- So "odds" asks every human battle through the vote dialog, which **defaults to auto-resolve after 15 s**.

**Port today:**
- app_screens.cpp:683-688: "never" sets `prompt_.no_autoresolve` (Begin only); "odds" uses the normal banner with no timeout.
- conflicts.cpp:655: the overrun gate checks only `story_locked`.

**Port change ([H]):**
1. app_screens.cpp:687: drop the kAutoResolveNever line, so "never" offers both buttons.
2. For completeness, add `bool overrun_off` to ConflictRules (or the manager's options), set when `game_options &&
   auto_resolve_option == 1`, and gate conflicts.cpp:655 with `&& !overrun_off`. It only matters for mods that set
   Overrun_Auto_Resolve_Multiple.
3. "odds": when `game_options && option == 3 && !story_pending`, use a vote-style prompt: a Tactical / Auto-Resolve dialog with
   the TEXT_ASK_* texts, auto-resolving after `AutoResolveVoteDefaultTimeOut` s of wall clock unless
   `AutoResolveVoteDefaultToTactical`.
   - A smaller step: keep the banner and add the timeout default.
   - Test: the prompt state machine (or `battle_prompt_rule(option, locked)` as a pure function): never -> two buttons, no
     timer; odds -> two buttons, timer 15 s, default AutoResolve; ask -> no timer.

**Load (BATTLE_TRANSITIONS §1.6) [H]: retail does NOT save the record.**
- The GMM save 0x28c900 / load 0x28b080 microchunks cover +0x18..+0xb8 and +0xf0..+0x1fc, but not +0xbc..+0xdf.
- The only serialiser of 0x140b1549c is the **replay** header ("temp.rec" writer 0x2c20d0, chunk 0x76, 0x24 bytes; reader
  0x2c1270).
- The other writers are game starts: campaign 0x957a0, 0x2b800, MP lobby 0xb6410 / 0xb99c0, skirmish 0xf2690, and the boot
  init 0x16fa0.
- After a load, the record is whatever is in memory. On a fresh launch that is the defaults (0x20 clear), so it asks. Within one
  run it is the last started game's options (a carry-over quirk [M]).
- **No save-format change.** The port's load -> ask matches the fresh-launch case. Update the session_spec.hpp:74-75 comment to
  cite this.

**Save-format flag:**
- The port writes `spec.interventions_off` (game_session.cpp:1272 / 1501). Retail does not save it.
- After a fresh-launch load, retail's InterventionsEnabled is 1.0, because flag 0x20 is clear. Following retail means no longer
  reading the key on load (keep writing it, or drop it; a key that is absent is already read as on).
- That is a [port] decision, recorded here.

## Open
- t13 exact pixel geometry of the header and cells (0x2eb890 / 0x353ad0 offsets), and the gold vs green number colour
  (0x1f9470) [M].
- t13: the faction strings at tooltip +0x2708.. are not read directly [M].
- Outside these items: in single player, flag 0x20 with option 0 sets "always" even when the battle is story-locked
  (0x2b98a5 path, [H] branch), which gives the auto-resolve presentation (hook 0x140b15608). The port keeps locked battles asking
  (app_screens.cpp:685); this needs a check of hook 0x140b15608 [M].
