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
  the health bar: y + bar_h/2 + icon_h/2 + 3, shifted left by (weather_w/2 + icon_w/2)/2 when the weather icon shows.

### Overhead status record (0x2ddac0 build, 0x2e7fd0 + 0x31de70 update; tactical only) [H]
0x302620 runs 0x31de70 when HUD +0x211c > 1 (2 land, 3 space) and +0x26a5. Units: the hovered object (+0x2030) and the local
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
- **lt_weather_icon 0x104 [M]:** shown iff 0x2f9950: the unit's weather modifiers (0x468750 accuracy for its projectile categories
  4/6/7, ability 0xb, 0x468510 sight/speed) fall below runtime thresholds DAT_140b16bbc / bc0 while weather is on (+0x448 > 0.5).
  Texture: current weather entry name minus 14 chars, prefixed "i_pa" [H code, L result]. Above the ability icon.
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
icon). "Allied" is WorldHud's hostile predicate, which the game host does not set yet (#536).

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
Hovering it gives cursor 0x1d (0x2f5fee). Port: sim TacticalBattle::tactical_sell (tactical_battle_bt.cpp:345) exists, no UI.

### skirmish_upgrade (0x232): tactical "sidebar" column [H]
0x317480 rebuilds two lists (HUD +0x1130 / +0x1148, the top / bottom sets of the XML Size): every live object owned by the
local player or an ally whose type has Show_In_Sidebar_When_Complete (+0xca), icon = Icon_Name (+0x1820, "missing.tga" if
empty), ordered unless Show_In_Sidebar_Tray (+0xcb); upgrade chains via Previous/Next_Upgrade_Level (+0xa60 / +0xa70) share one
slot (0x2deba0); in-progress production (player +0x3a0 queues 2..3) with Show_In_Sidebar_While_Building (+0xc9) shows the icon
with fill = progress and "%d%%". Plus: space with weather -> the weather icon slot first; land skirmish (0x2ded90, player kind
3 via 0x282750) -> i_interdictor_block slot [M meaning]. Large family.

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
3. **st_ability_icon** [H texture rule]: HudUnit `std::string ability_icon[2]` filled from the provider's HudAbility (icon,
   active) for allied units; draw above the bar.
4. **tactical_sell** [H]: input hover/click on an own object with the sell behaviour -> HUD button text above -> HudAction ->
   tactical_sell command. Medium (input + HUD + text).
5. cs ability palette (trigger [L]), skirmish_upgrade sidebar (large), lt_weather_icon / surface_mod_icon ([L] parts),
   tactical bribe_display (position [L]): not ready.

## 3. Open (needs-info)
- cs_ability palette trigger: what 0x398470 / the 0x2ad280 branch returning 0xb means in play.
- lt_weather_icon thresholds (DAT_140b16bbc / bc0, runtime) and the texture name derivation; surface_mod_icon's obj +0xf0 class.
- bribe_display tactical position; skirmish_upgrade's kind-3 interdictor slot meaning.
- Pixel placement of 0x352650 positions (centre vs corner) for the respawn counter [M].
