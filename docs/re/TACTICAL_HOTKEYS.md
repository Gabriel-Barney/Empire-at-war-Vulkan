# Tactical hotkey dispatch, per slot

Retail FoC x64 `StarWarsG.exe`, read in the Ghidra export and the disassembly (#430, 2026-11-24). Tiers: **[H]** read
directly, **[M]** inferred with good support, **[L]** guess. Addresses are image offsets from 0x140000000. Slot numbers
are the port's `ui::hk` slots (the HotKeys.xml order).

## The dispatch [H]

`0x63f00(key)` returns at once in any of these cases:
- a GUI dialog is modal;
- there is no mode;
- the key is Esc;
- a global block is set.

Otherwise it maps the key to a slot through the applied table and switches on the slot.
- **Scroll keys:** slots 69-72 belong to the world key handler `0x33460`.
- **Not in this switch:** slots 77-79 belong to the battle-cam controller. Its key-down handler `0x2cbd00` runs before
  `0x63f00`, and its key-up handler is `0x2cc080`.

## Per slot

| slot | retail | tier | port |
|---|---|---|---|
| 11 SelectLike | the type of the own object under the cursor; every selectable object of that type in the view is **appended** | H (append M) | the same (`ui::select_like`, #461); "selectable" = the local player's objects on screen [port] |
| 12 / 13 Next / Prev | empty selection: select the first own object, no camera move; else the object after the selected one, with a camera move | H (order M) | the same (`ui::unit_cycle`, #461); the candidates are one unit per squadron, ascending id [port] |
| 14 / 15 / 17 / 18 / 23 Guard / Attack / Move / Waypoint / AttackMove | **toggle** a flag; no exclusivity, no sound, no button test | H | sets the flag; Attack / Move gated on the button (#430 part 2) |
| 20 Reinforce | `0x2e5580`, the same as the `b_reinforcement` click: opens or closes the reinforcement list | H | the same (#463); see "Reinforce" below |
| 21 Place Beacon | `0x2e7e40`, the `b_beacon_t` click: armed → disarm; else arm when the cooldown is 0. **No sound** during the cooldown | H | matches |
| 22 SuperWeapon | ready local SPECIAL_WEAPON objects first, in any mode; then the land bombing run | H | the land path skips the first step: #464 |
| 24-66, 90-122 abilities | `0x2dccf0(HUD, type)` clicks every ability button of that type | H (click M) | the first matching button |
| 37 SystemControl | type LURE (shares slot 52's call) | H | done |
| 43 SwapWeapons | SWAP_WEAPONS, then ROCKET_ATTACK | H | done |
| 109 ForceCloak | FORCE_CLOAK, then STEALTH | H | done |
| 56 DestroyPlanet | no ability; the galactic planet-destroy UI | H | "" is right |
| 76 Overview | only outside a battle-cam view | H | no gate: #462 |
| 77 Camera Tether | on → off; off → follow the **first selected object**; nothing with an empty selection | H (the follow M) | done |
| 78 Movie Mode | key up: the battle cam on the selected objects (state 3); Esc exits | H | only hides the HUD: #462 |
| 79 Cinematic View | needs `In_Game_Cinematics`; steps through 8 views; Esc exits | H | nothing: #462 |
| 80 Screenshot | `ScreenShots\EAW_<YYYYMMDD>_<HHMMSScc>.tga` on the next frame | H | the same name, `.png`, in a ScreenShots folder beside the saves [port] (#466) |
| 81 / 82 Quick Save / Load | slot name `L"[QuickSave]"`; nothing in multiplayer | H | done |
| 83 Mission Holocron | toggles the objective display | H | matches |

## The order buttons and their flags [H]

`c_button00`..`05` toggle the same flags as the keys. Differences from the keys:
- Arming Attack (`c_button00`) or Move (`c_button02`) clears the other four flags and plays a sound.
- Waypoint, AttackMove and Guard have buttons of their own (`c_button03`, `01`, `05`).

**With Attack armed:**
- a right click attacks an attackable object;
- on the ground, or on an own or friendly object, it gives **no order**.

**With Move armed:** a right click is a plain ground move.

**A left click on a non-order target** clears Attack (else Move) instead of deselecting.

**Who clears the flags:**
- A ground move or radar move clears Guard, Attack and Move only.
- AttackMove and Waypoint survive a move order.
- The legacy P1_DOUBLE_CLICK_MOVE §2.1 step 4 ("all five cleared") is wrong for those two.

## Battle end: Invade System [H, M]

Both battle-end openers (`0x87770` @`0x87c0d`, `0x88050` @`0x88303`) hide `IDC_LAND_INVADE_BUTTON` [H], and nothing
shows it [M]. The port hides it too (`GuiScreens::show_battle_end`, #465). The name's only other reference is its click
handler [M], which cannot be reached, so the port has none.

## Reinforce: the reinforcement list (0x2e5580) [H]

R and the `b_reinforcement` click both call `0x2e5580(HUD)`. Component 0x218 is `i_main_reinforce` and 0xe7 is
`b_reinforcement` (the name table at `0xa174e0 + 0x48*id`). Nothing happens unless four gates pass [M names]:
- HUD +0x211c > 1;
- the local player's +0x39c flag;
- HUD +0x26a0 < 1;
- the input owner's (`0xb15418`) +0x28 object has +0x20 clear.

Then:
- **Hide:** a shown `i_main_reinforce` is hidden (vf +0x78(1)).
- **Show:** a hidden one is shown unless `0x3400b0` reports a held object (+0x1c != -1). Showing it:
  - places it at (-w/2, h/2 - 100) (vf +200);
  - plays the local faction's `Reinforcements_Selection_SFXEvent` (+0x2250);
  - refreshes the HUD texts (`0x316260`, among them `r_pop_text` 0x22f);
  - flashes `b_reinforcement` (vf +0x120).

The port (`TacticalHud::toggle_reinforce_list`, `App::toggle_reinforcement_list`):
- the list is the `r_RRCC` pad with `r_pop_icon` / `r_pop_text`, drawn only while open;
- [port] it opens in a battle when the controls are not story-locked and no drop point is being picked;
- a pad entry starts the drop pick, as before;
- [port] while a drop point is being picked, R closes an open list but does not reopen it, and the refusal is silent (as
  retail's held-object test, which plays nothing); right-click and Esc cancel the pick, R no longer does;
- [port] the `i_main_reinforce` shell model itself is not loaded: the pad keeps its fallback positions.

## Open

- The battle-cam key gates.
- The names of the reinforcement list's four gates [M].
- The `i_main_reinforce` shell model and its placement (-w/2, h/2 - 100): not loaded, the pad keeps its fallback positions.
- The camera consumer of the tether target [M].
- What the battle-cam cameras frame.
- The Next / Prev filters and order.
