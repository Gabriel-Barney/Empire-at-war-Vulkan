# Tactical command-bar components with no port counterpart

Retail FoC x64 `StarWarsG.exe`, read in the Ghidra export and the disassembly (#296, 2026-11-24). Tiers: **[H]** read
directly, **[M]** inferred with good support, **[L]** guess. Addresses are image offsets from 0x140000000.

Follow-ups: garrison_respawn_counter #474, overhead status icons #475, st_ability_icon #476, tactical_sell #477, skirmish_upgrade #478.

## 0. Table layout (both readings are right) [H]
`FUN_140347b50(id)` returns `(&DAT_140a174e8)[id*9]`, i.e. the component pointer at **0xa174e8 + 0x48*id**; the name pointer is at
**0xa174e0 + 0x48*id** (0x347b70 scans from `PTR_s_i_main_commandbar_140a174e0`, so id 0 = i_main_commandbar). The issue's
"0xa174e8, name at +0x40, id = index + 1" is the same address (0xa174e8 + 0x48*(id-1) + 0x40); #437's "0xa174e0, name +0,
component +8" is the natural base. Other getters on the same slot: 0x347af0, 0x347b20 (vf +0x80), 0x347bf0 (vf +0x78 =
hide(1)/show(0)), 0x347c30 (vf +0x148 = set text), 0x347c70, 0x347cd0.

Ids: building_cap_text 0x2b8, building_cap_number 0x2b9, tactical_sell 0x217, skirmish_upgrade 0x232, garrison_slot_icon 0x105,
garrison_respawn_counter 0x231, st_garrison_icon 0x103, st_ability_icon 0x102, st_hero_icon 0xf8, st_hero_health 0xf9,
lt_weather_icon 0x104, remote_bomb_icon 0x380, bribe_display 0x339, bribed_icon 0x37e, surface_mod_icon 0x37f,
cs_ability_button 0x278, cs_ability_text 0x279, ds_shell 0x106.
Search: every one of the 717 `call 0x347b50` sites with an immediate ecx (s296/ecx.py), the other getters (decomp grep), rip refs
to each slot (none) and `cmp/sub reg, id` in 0x2d0000-0x3f0000 (s296/cmpscan.py).

## 1. Answers by family

### Caps: galactic, already ported [H]
building_cap_text 0x2b8 is labelled TEXT_BUILDING_CAP in 0x3c5fc0 (planet panel, next to TEXT_UNIT_CAP); building_cap_number
0x2b9 gets "%d/%d" = 0x3f8af0(planet) / 0x372740(planet type, planet) in 0x3e4cb0 (0x347c30(0x2b9) right after 0x2b7). Only
galactic code names them. Port: galactic_hud.cpp:238/245 already has both. **Drop.**

### Selected-type icons
- **st_hero_icon / st_hero_health: dead [M].** No lookup anywhere (no immediate id at any 0x347b50 / getter call, no slot ref);
  the per-unit grab icon is st_grab_bar 0xf3 + st_health_bar 0xf4 (0x3120c0 -> 0x2e9800, record at HUD +0x1ff8), which the port's
  unit icon already covers. Only trace: 0x2e1021 tooltip `cmp ecx, 0xf8` (a branch, no producer). **Drop.**
- **st_ability_icon (0x102): tactical, per-unit overhead icon [H code, M "active"].** See "Overhead status record" below: record
  [3]. Texture set in 0x2e7fd0 only for a unit allied to the local player (0x39a2c0 -> 0x2823e0, relation 0): for ability slot
  s = 0, 1, kind = 0x2f62f0(hud, obj, &type, s) (0 = none; also 0 while the record (+0xf0) cooldown timers block it, or
  0x39bbd0 refuses); state from 0x2f9870 (3 = recharging) or 0x39b480 && (0x39b950(obj,kind,1) ? 2 : 1). Only state **2** sets
  a texture: slot 0 -> vf +0xa0 (icon), slot 1 -> vf +0xa0 if slot 0 had none, else vf +0xd8 (second layer). Texture =
  0x43eaf0(&DAT_140b2f2e0, slot, type): the ability's own icon string (ability +0x200 when +0x210 != 0) else the per-kind default
  (table 0x30 per kind). Not allied: both textures cleared. Shown and placed every frame (0x31de70: vf+0x78(0)) centred above
  the health bar: y + bar_h/2 + icon_h/2 + 3, shifted left by (weather_w/2 + icon_w/2)/2 when the weather icon shows (ported, #296).

Port (#476): `ui::overhead_ability_icons` gives the unit's two icons from `abilities_from_s9` over that one unit: for slots 0
and 1, the HudAbility icon (Alternate_Icon_Name, else `ability_icon(type)`) when the ability is active and not recharging
(state 2), else none. [port] a slot is the unit's command-bar button order (ascending ability type), not the XML slot order;
the 0x2f62f0 gates (the record cooldown timers, 0x39bbd0) are not ported, and 0x39b480 (unidentified) is not mapped. app_hud.cpp fills `HudUnit::ability_icon` for
selected and hovered units. `WorldHud` draws them for allied units on st_ability_icon's Base_Layer at atlas size x Scale:
slot 0's, then slot 1's as a second layer sharing its centre (the first layer when slot 0 has none). [port] centred 3 px
above the top of the bar stack, not the health bar, because the port stacks the shield and power bars over the health bar;
the 3 is read as pixels [M]. [port] none while the health bar is hidden, and nothing (not the component's own
Icon_Texture_Name) when no slot is active: whether retail keeps the last texture then is not traced. The weather shift is ported with lt_weather_icon (#296). "Allied" is the host's diplomacy predicate (#536).

### Overhead status record (0x2ddac0 build, 0x2e7fd0 + 0x31de70 update; tactical only) [H]
0x302620 runs 0x31de70 when HUD +0x211c > 1 (2 space, 3 land: 0x28b080 maps mode type 1 (land) to 3 and type 2 (space) to 2 [H], lane-c 2026-11-26) and +0x26a5. Units: the hovered object (+0x2030) and the local
player's selection (0x2ad080). Per unit a 0x58 record at HUD +0x2018: [0] health bar, [1] shield bar, [2] st_control_group,
[3] st_ability_icon, [4] st_garrison_icon, [5] lt_weather_icon, [6] array of garrison_slot_icon (count = type +0x2360
Num_Garrison_Slots, created hidden), [7] bribed_icon, [8] remote_bomb_icon, [9] surface_mod_icon. All hidden when 0x35f470
(not visible to the player) or 0x2e7fd0's off-screen flag. Port counterpart: WorldHud (engine/ui/src/hud_world.cpp, the
bars / brackets / control group of selected-or-hovered units) fed by HudUnit (hud_world.hpp) from app_hud.cpp:950.
- **bribed_icon 0x37e [H vis, M meaning]:** shown iff obj byte +0x3cc != 0 (0x31de70); texture = own Icon (i_sa_tactical_bribe.tga)
  for an allied unit (0x2e7fd0). +0x3cc is set to 1 only by 0x5b07c0 (Tactical_Bribe apply, after the convert) [H], so "this
  unit was bribed". Placed right of the health bar: x + bar_w/2 + icon_w/2, bar y.
- **remote_bomb_icon 0x380 [H]:** shown iff 0x3972c0(obj) > 0 = obj +0x328 summed over team members; +0x328 += 1 in 0x38d320
  (attach to parent at a bone) when the attached object's type has Friendly_Damage_Amount (+0x23ec) > 0 - only
  Proj_Remote_Bomb (200) in FoC data. So "a remote bomb is stuck to it". Texture i_icon_bomb.tga. Right of the health bar, plus
  the bribed icon's width when that shows.
- **st_garrison_icon 0x103 [H]:** texture i_garrison_flag.tga iff obj +0x3a2 & 2 (else none), allied units only; always "shown",
  right of the health bar (same spot as bribed_icon). Bit 2 is set by the spawners 0x2a5a60 (param 6) and 0x2a00b0 (source
  +0x3a6, which 0x2a1eb0 sets for the start-force garrison spawn) [H]: the skirmish start-force / spawned garrison units.
  Same flag also swaps the grab-bar icon's second layer to this texture (0x311ab0 land branch).
- **garrison_slot_icon 0x105 [H]:** one per Num_Garrison_Slots; hidden if obj +0x3a0 & 0x80, else shown with alternate texture 0
  (i_dialogue_radio_on) for slot i < cap - free and its Icon (radio_off) otherwise, free = 0x3954b0 = Num_Garrison_Slots minus
  the summed Garrison_Value (+0x23ac) of the contained objects (obj +0xf8 list). So occupied slots lit, left to right. Row centred
  on the bar x, below the bar (y - (h - DAT_140b2767c)), step = icon width.
- **lt_weather_icon 0x104 [H] (#296):** status slot 5. Shown iff the gate 0x2f9950 holds for the unit, or, through behaviour 0x16, for any member of its team:
  - the weapon at obj +0x324 in the type's Projectile_Types (+0xc48) has Projectile_Category (weapon +0x1fe8) 4, 6 or 7, and the current weather's accuracy modifier 0x468750 for that category is below `Min_Accuracy_For_Icon` (DAT_140b16bbc, GC +0x13bc, FoC 0.4);
  - or the object has TURRET (behaviour 0x0b), and category 4's modifier is below that value;
  - or its sight modifier 0x468510 is below `Min_Sight_Range_For_Icon` (DAT_140b16bc0, +0x13c0, 0.5).

  The texture is the current weather record's Icon_Name (0x468860; the default record while the blend +0x448 <= 0.5) with its first 14 characters dropped and `i_pa` (0x85d670) put in front: `i_encyclopedia_weather_rain.tga` becomes `i_pa_weather_rain.tga`.

  Placement: centred above the health bar, y + bar_h/2 + icon_h/2 + 3. While the ability icon shows, the two move apart by (weather_w/2 + ability_w/2)/2: the weather icon right, the ability icon left.

  Port: `TacticalBattle::UnitHudStatus::weather_accuracy` / `weather_sight` (a Weatherproof owner reads 1.0 [M]) and `ui::weather_status_icon` give `HudUnit::weather_icon`. `WorldHud` draws it as above the port's bar stack, for any owner. [port] the team walk is the member's own check, because the HUD's units are the members.
- **surface_mod_icon 0x37f [H code, L source]:** shown iff the string at (obj +0xf0) + 0x168 (or a team member's) is non-empty;
  that string is the texture. The obj +0xf0 record class (save tag 0x29) is not identified. Left of the health bar.

Port (#475): `TacticalBattle::unit_hud_status(id)` reads the four inputs: bribed from `AbilitySystem::bribed` (a saved per-object
flag set at the Tactical_Bribe owner change, [port] also true for a member of a bribed team container), bombs from
`CombatSystem::stuck_bombs` summed over a team's members, the garrison flag from `flags_3a2 & 2` or [port] the SkGarrison
start-force ids (the port does not set bit 2 on the start force, so the hangar engine check is unchanged; re-delivered units are
not flagged), and the slots from GarrisonSystem capacity / used. app_hud.cpp copies it into `HudUnit` for selected and hovered
units; `WorldHud` draws the icons beside the health bar, so [port] none while the health bar is hidden. The slot row sits just
under the bar [port]; retail's offset constant is not read. Not ported: the slot row's hide test (obj +0x3a0 & 0x80, the death
clone bit, which a selectable unit does not carry), and any bribed-icon width a hostile bribed unit may reserve before the bomb
icon (retail shows bribed_icon by +0x3cc but gives it a texture only when allied; the port adds the width only when it draws the
icon). "Allied" is WorldHud's hostile predicate, which the game host sets from the battle's diplomacy (#536).

### Garrison respawn counter (0x231): tactical HUD button [H rule, M position]
Created once (0x2e9700, HUD +0x2028), texture = Icon i_button_skirmish_reinforcements.tga, Build overlay i_button_build.tga.
- Show: 0x280c50 (player) when 0x28afb0 and player +0x2f8 == 0 and +0x2e8 == 0 (start-force garrison gone): arms the timer
  (+0x2f0, duration Faction Garrison_Reinforcement_Delay_Seconds +0x2ee4) and, for the local human player (+0x60), 0x30a8b0
  shows it with fill 0.
- Value: each player tick 0x287e70 -> 0x311a60 vf +0xf0(fill) with fill = 0x4ad2a0 = elapsed / duration (0 -> 1).
- Hide: at expiry 0x287e70 -> 0x2f8d80 for the local player, with Reinforcements_Ready SFX (faction +0x22a0).
- Place: 0x318720 at (30 - W/2, H/2 - 60) in the centred GUI space -> 30 px right, 60 px down from the top-left [M]; the hero
  grab-bar icons (+0x2008) follow to its right, step = width + 5.
Port (#474): `TacticalBattle::garrison_respawn(player)` gives the fill from the SkGarrison timer (-1 = none); the host
feeds the local player's to `TacticalHudFrame::garrison_respawn`, and `TacticalHud::build_garrison_respawn` draws the
component with its dial at the [M] point above.

### Tactical sell (0x217) [H trigger/text, M click path]
Hover action 10 (0x2ad280): the object is the local player's (+0x58) and has the sell behaviour slot (+0x35d != -1), not in a
build placement (DAT_140b29fc0) -> cursor 0x1d (TacticalSell, 0x38b60). Click -> 0x41f70 case 9 "Activate command bar tactical
sell menu" -> 0x2dd250: one clone at HUD +0x20d8 (user data 0 = object), state +0x20b4 = 0, text
`L" %s %s ($ %d) "` = TEXT_BUTTON_SELL, the type's name (type +0x140), refund 0x374590(type, obj); width from the text.
0x2fcf70 keeps it at the object's projected point (+ the drag offset); it goes when the object dies / is removed (+0x3a0 & 2).
Hovering it gives cursor 0x1d (0x2f5fee).

Port (#477):
- **Offer.** `TacticalBattle::sell_refund(store, player, object)` (const) answers with the refund `tactical_sell` would credit:
  the same pad lookup and amount (`pad_sell_refund`), for a live object of the player's with the TACTICAL_SELL behaviour (the
  +0x35d test) that is also the built object of a pad ([port]: the only objects the sim's sell accepts; the sim's command
  itself does not test the behaviour). A locked pad still answers; the command is then refused.
- **Cursor.** `fill_cursor_hover` sets `CursorContext::sellable` for an own object with an offer, and `choose_pointer` gives
  POINTER_TACTICAL_SELL for it, selection or not ([port] before GARRISON_IN; the hover actions' order is not traced).
  `over_sell` gives the same pointer over the button.
- **Button.** A world left click on such an object opens the button (`App::sell_object_`) and is spent there, as 0x41f70
  case 9 suggests: no order, no selection. [port] any other world left click closes it (retail's close path besides
  death is not traced), and so do the sell click, the offer ending (sold, dead, changed owner) and a new battle.
- **Draw.** `TacticalHud::build_sell` draws tactical_sell at the projected point with the caption `tactical_sell_text`
  (" TEXT_BUTTON_SELL <Text_ID name> ($ <refund>) ") in the component's font, colour and outline. [port] the button is the
  Icon_Texture_Name (Mouse_Over_Texture_Name while hovered) stretched to the text's width at its atlas height x Scale; the
  drag offset is not ported.
- **Click.** A left click on it sends TacticalSell {object} (command 34), [M] as retail's click path.

### skirmish_upgrade (0x232): tactical "sidebar" column [H] (#478, #548)
**Lists `0x317480`.** The HUD update `0x30c3b0` runs it every second frame (the +0x2120 toggle), in tactical modes only.
Each slot is a clone of 0x232.
- **Mark:** every slot of list 0 (+0x1130) and list 1 (+0x1148) is marked stale.
- **Completed:** the object manager's DUMMY_UPGRADE list (+0x378: obj +0x397 != -1, the slot of behaviour 0x65), in insertion order. It keeps:
  - live objects only (obj +0x3a0 & 2 clear);
  - objects whose owner is the local player or allied to it (`0x2823e0`, diplomacy 0);
  - types with Show_In_Sidebar_When_Complete (+0xca).

  Each goes into list 0 through `0x2deba0`, at the front when Show_In_Sidebar_Tray (+0xcb) is false. The slot gets:
  - Icon_Name, or missing.tga when empty;
  - fill 1.0 and no text.

  A Tray = False slot also places a `p_icon_corrupt.alo` model.
- **In progress:** queues 2 (Tactical_Units) and 3 (Tactical_Upgrades) of the local player and its allies, the head item
  only. The item needs DUMMY_UPGRADE in its generic set (`0x370320(t, 0x65, -1)`) and Show_In_Sidebar_While_Building (+0xc9).
  - **No Previous level:** a list-1 slot.
  - **Otherwise:** the list-0 slot of the Previous level, with user[2] = the item.

  The slot gets the item's icon, the fill p and the text `"%d%%"` of (int)(p × 100).
- **Interdictor `0x2ded90`:** space (+0x211c == 2), not a skirmish (`!0x28afb0()`), and `0x282750(local) == 3`, the
  interdicted retreat refusal. The slot shows Icon_Alternate_Texture_Name[0] (i_interdictor_block) and has
  user[2] = 1. Its tooltip (`0x2e32f0`) is TEXT_ENCYCLOPEDIA_RETREAT_UNAVAILABLE. This corrects the old "land skirmish".
- **Weather:** land (+0x211c == 3), when `0x468860(weather)` names a texture. That record is the current type's above
  intensity 0.5, else the clear record. [M] The texture is the WeatherModifiers Icon_Name. This corrects the old "space".
- **Sweep:** every slot still stale is removed.
- **Chain sharing `0x2deba0(type, list, at_front)`:**
  - a slot of `type` is kept;
  - a slot of a lower level of its chain (Previous walk) takes `type` in place;
  - a slot of a higher level (Next walk) means `type` is not shown;
  - otherwise a new slot is made.

  So there is one slot per chain, showing the highest completed level.

**Placement `0x301190`** runs every frame in tactical modes.
- **The data:**
  - Offset (−19, 309) on top of x = good_hero_frame's x (HUD +0x10e8, through component 0x27d);
  - Default_Offset (50, −38), replaced by Default_Offset_Widescreen at an aspect of 1.75 or more when that is set;
  - Size.x = −5, the gap;
  - Size.y = 0.8, the icon scale [M];
  - Icons_Per_Column from GameConstants (+0x124c, 13).
- **The top set:** the specials, then the run of Tray = False slots of list 0. They go down one column with no wrap.
- **The gap:** y += Size.x.
- **The tray:** the rest of list 0, then list 1. It wraps every Icons_Per_Column rows to x + 50, and each new column starts
  back at Offset.y.
- **Frame piece (Blank_Texture_Name index):**
  - a lone tray slot: whole;
  - first: top, last: bottom, the rest: middle;
  - list 1 alone: top for every slot (a retail quirk);
  - the weather slot: whole, or top over an interdictor (bottom);
  - Tray = False slots: none.

**Port** (`engine/ui/upgrade_sidebar.hpp`, `TacticalHud::build_sidebar`, `TacticalBattle::sidebar_feed`):
- **The rules:** the list rules and the placement above, with tests (`test_upgrade_sidebar.cpp`, `test_hud_stage2.cpp`).
- **The lists** rebuild every frame instead of every second frame.
- **The specials** stay at the front. Retail reuses list0[0] / [1] for them, so it can repaint an older slot (a quirk not kept).
- **The anchor:** good_hero_frame's x is taken as the canvas's right edge, where the port pins the local hero bar [L].
  The 1024 × 768 GUI units map one to one to canvas units [M].
- **Not drawn:** the interdictor tooltip and the `p_icon_corrupt.alo` models. The sidebar has no hit boxes.
- **"Object order"** is the port's insertion order of the battle's objects [M].

### Corruption / FoC
- **cs_ability_button / cs_ability_text: tactical ability palette [H build, L trigger].** Action 11 (0x2ad280, 0x398470(obj): an
  own unit with abilities in a selection state) -> 0x41f70 case 10 "Activate command bar tactical special ability palette menu"
  -> 0x307de0: for the clicked allied unit, button+text per ability slot (slot 1 hidden if none), icon 0x43eaf0, fill =
  recharge 0x3108b0, text = the ability's name text (0x43f340); placed at the click point (+0x212c) + Offset, rows by
  Default_Offset. 0x2ff5a0 refreshes the fill and closes it when the cursor is > 150 px away or the unit is gone.
- **bribe_display 0x339: both.** Galactic drag cost (0x3e1500, red when > credits) and tactical: 0x2ad280 -> 0x3e3f20 while
  bribe-targeting a valid target: TEXT_BRIBE_DISPLAY_TEXT + cost 0x5b0ec0 = Tactical_Bribe_Cost x Bribe_Modifier_On_(Un)Corrupted
  [H]; hidden otherwise. Position: none set in code [L].
- **ds_shell 0x106: dead [M].** No lookup; the Death Star switches are deathstar_switch etc. (port already handles them).

### Orbital bombardment switch: pb_switch (0x379) and pb_switch_recharge (0x37a) [H]
The human player's land orbital bombardment (#717). The commandbar data makes pb_switch a TextButton (Icon_Texture_Name
`i_button_special_pb_switch_03`, Icon_Alternate_Texture_Name `_00`) and pb_switch_recharge a Smooth_Bar Bar.
- **Update 0x314360, every frame.** In land (+0x211c == 3) with the PlanetaryBombardManager (mode +0x40): shown =
  0x528a10(manager, local player), the record's enabled byte, gated as the bombing run's 0x494410 (no global suppress; not a
  skirmish, or the skirmish's special-weapons flag 4). Shown: both unhidden; ready = 0x528970 (enabled, available +1, not
  in progress +0x20); the icon is Icon_Alternate_Texture_Name entry 0 when ready, else 0x34c340 (the Icon_Texture_Name);
  vf+0x80(!ready) disables it; the bar gets 0x5294d0's fraction (0x351a90). Not shown, or space: both hidden. Either way it
  moves both components' bones in the shell model (z 8 shown, -10 hidden) [port: not done, the shell stays still].
- **0x5294d0, the bar** (record 0x48 bytes: +0 enabled, +1 available, +4 ready frame, +8 interval, +0x10 last start, +0x20
  in progress, +0x40 timer only). Not enabled: 0. Not timer only and the player's gate 0x5275a0 closed: 0. Available and
  idle: 1. Else (now - S) / (E - S), with E = the ready frame when idle, now + interval in progress: 1 once now >= E, 0 when
  E <= S, S == -1 or now <= S. [M] with the gate open the original also returns 1 in the game modes 0x28af80 / 0x28b060
  name; not ported, as for the bombing-run dial 0x495250.
- **Click 0x2e7bc0** (the switch's handler, through the thunk 0x347930): flips HUD +0x2ae6. Off -> on in land: 0x5296d0
  plays the record's ship type's +0x1360, which is SFXEvent_Bomb_Run_Select_Target (SFXEvent_Bombard_Select_Target +0x1380
  is read by nothing, so stock data starts the pick silently); then the blob (+0x2ae8) with the local faction's
  Bombardment_Shadow_Blob_Material_Name (+0x2700), white. On -> off: the blob is freed and the local faction's
  SFXEvent_Bombard_Cancelled (+0x2050) plays, 2D.
- **While on** (0x30c3b0): the blob follows the cursor, green when 0x528a70 accepts the point, red otherwise, with a full
  side of 2 x Bombardment_Blob_Size (+0x2720; the bombing run's is Bombing_Run_Blob_Size itself). The cursor is the
  bombing run's (0x2f5c80 returns 0x21 for both).
- **World click 0x2f6c30:** on a point 0x528a70 accepts it sends event 54 PlanetaryBombard (0x52afc0 / 0x5247a0, then
  0x3b01f0), plays the ship type's SFXEvent_Bombard_Incoming (+0x1390) through 0x5295f0 (its only caller, so an AI or Lua
  bombardment plays no Incoming line), frees the blob and clears +0x2ae6.
- **Execute 0x52aff0:** dequantizes the point, |z| < 0.0001 becomes 0; with the manager, 0x528a70 then 0x528140 (the start).

Port: `TacticalBattle::bombardment_shown` / `bombardment_recharge` / `bombardment_ship_type`, the PlanetaryBombard handler
in `handle_command`, `ui::HudBombardSwitch` (`TacticalHud::apply_bombard_switch`, `build_bombard_recharge`),
`game::bombard_switch` / `bombardment_style`, and `App::bombard_toggle` / `bombard_end` beside the bombing-run pick.

### Death Star switch: deathstar_switch (0x107) [H]
The human player's tactical super weapon button in space (#718). A TextButton with Can_Animate (5 fps, no loop).
- **Update 0x31acd0, every frame.** Space only (+0x211c == 2). Shown when the local faction's name equals the HUD string
  +0x2728 (copied from 0xb16b20, "Empire"), the object list has an object with the TSW behaviour (0x2c; the first one,
  any owner) and HUD +0x26a0 <= 0 [L: its meaning; not ported]. Shown: unhidden, +0x2041 set; ready = 0x42a6b0 (the
  TSW in state 4). Not pressed (+0x2040 clear): ready -> Icon_Alternate_Texture_Name 0, enabled; else 0x34c340 (the
  Icon_Texture_Name), disabled. Pressed: disabled, the icon left as it was. Its shell bone is raised (z 8) while shown
  [port: not done]. Not shown: hidden, the HUD's +0x2050 object hidden, the bone override off.
- **Click 0x2f4e90** (the handler, through 0x3470f0; also hotkey case 0x38 of 0x63f00 [port: not bound]): only when not
  pressed. It plays the switch's animation with 0x2f4f30 as its end callback (vf+0x2e8), sets +0x2040 (cleared only by
  the battle reset 0x2f8ff0, so once per battle), hides +0x2050, and with +0x2af0 set pokes object +0x2af4's behaviour
  0x46 (0x42fb70(.., 0x37)) [L; not ported].
- **0x2f4f30:** the first TSW object; when it is the local player's, 0x42b8d0(behaviour); a 0 result plays the GUI
  negative SFX (0x140b301c8).
- **0x42b8d0:** the planet gate 0x42a720 failing plays the negative SFX and returns 0 (so the caller plays it again).
  State 0 with a target (0x42a330) builds event 38 op 1; state 4 op 2; any other state returns 0. The event takes the
  weapon id (0x3b01e0) and is sent (0x3b01f0); then the type's TSW_SFXEvent_GUI_Bttton_Press (+0x2150) plays, 2D.
- **Execute 0x5d6cd0:** the weapon's owner must be the issuer and it must carry the behaviour; op 1 -> 0x42a140 (the
  planet gate, state 0, a target dummy: -> state 2 with the target), op 2 -> 0x42a200 (Fire). The TSW's first service
  0x42a7b0 does 0x42a140's work itself for a defender, and its switch has no case 0, so retail's state 0 waits for op 1.

Port: `TacticalBattle::tsw_planet_ok`, `SpecialWeaponSystem::first_tsw`, the TacticalSuperWeapon handler in
`handle_command` (op 1 is refused: the port's TSW is in state 0 only before its first service and after its release),
`ui::HudTswSwitch` (`TacticalHud::apply_tsw_switch`), `game::tsw_switch` and `App::tsw_press`, which acts at once instead
of after the switch's animation and sends op 2 only (state 0 takes the refusal path, for the reason above).

## 2. Port today and changes, ranked (smallest and most certain first)

1. **garrison_respawn_counter** [H rule]. sim: TacticalBattle `std::optional<std::pair<float,bool>> garrison_respawn(player)`
   returning fill = (frame - (timer - frames_trunc(delay*30))) / frames_trunc(delay*30) while SkGarrison.timer >= 0 (recompute the
   delay as sk_garrison_tick does, no save change). ui: TacticalHudFrame field `float garrison_respawn = -1` (< 0 hidden); in
   tactical_hud.cpp draw component garrison_respawn_counter at pixel (30, 60) [M] with build_fill = fill*100 (as the queue,
   tactical_hud.cpp:1038); app_hud.cpp sets it for local_player(). Test (test_hud_draw.cpp): -1 -> no quad; 0.25 -> the icon +
   build overlay at 25 %.
2. **Overhead status icons in WorldHud** (bribed_icon, remote_bomb_icon, st_garrison_icon, garrison_slot_icon) [H rules].
   Done in #475 (see "Port (#475)" above); the plan as written:
   HudUnit gains `bool bribed, bomb, garrison_flag; uint8_t garrison_slots, garrison_used`; WorldHud::bind finds the four
   components; draw for selected/hovered units with the placements above (bribed/flag right of the health bar, bomb right of
   that, slot row below). Data: flags_3a2 bit 2 exists (tactical_battle.hpp:1930) but is not set for skirmish start-force units
   (SkGarrison ids) - set it there; garrison slots from Num_Garrison_Slots / Garrison_Value of the garrison contents; bribed needs
   a per-unit flag set where AbilityEventKind::BribeApplied fires; bomb needs the attach target of the Remote_Bomb projectile
   (not tracked today). Test: HudUnit{selected, bribed} -> a quad with i_sa_tactical_bribe.tga right of the bar; slots 3/1 ->
   one radio_on then two radio_off.
3. **st_ability_icon** [H texture rule]: done in #476 (see "Port (#476)" above). The plan was HudUnit
   `std::string ability_icon[2]` filled from the provider's HudAbility (icon, active) for allied units, drawn above the bar.
4. **tactical_sell** [H]: done in #477 (see "Port (#477)" above). The plan was input hover/click on an own object with the
   sell behaviour -> HUD button text above -> tactical_sell command.
5. cs ability palette (trigger [L]), skirmish_upgrade sidebar (large), lt_weather_icon / surface_mod_icon ([L] parts),
   tactical bribe_display (position [L]): not ready.

## 3. Open (needs-info)
- cs_ability palette trigger: what 0x398470 / the 0x2ad280 branch returning 0xb means in play.
- lt_weather_icon thresholds (DAT_140b16bbc / bc0, runtime) and the texture name derivation; surface_mod_icon's obj +0xf0 class.
- bribe_display tactical position; skirmish_upgrade's kind-3 interdictor slot meaning.
- Pixel placement of 0x352650 positions (centre vs corner) for the respawn counter [M].
