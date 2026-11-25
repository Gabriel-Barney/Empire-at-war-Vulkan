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
| 11 SelectLike | the type of the own object under the cursor; every selectable object of that type in the view is **appended** | H (append M) | the selected units' types, replacing the selection: #461 |
| 12 / 13 Next / Prev | empty selection: select the first own object, no camera move; else the object after the selected one, with a camera move | H (order M) | anchored on the last pick: #461 |
| 14 / 15 / 17 / 18 / 23 Guard / Attack / Move / Waypoint / AttackMove | **toggle** a flag; no exclusivity, no sound, no button test | H | sets the flag; Attack / Move gated on the button (#430 part 2) |
| 20 Reinforce | `0x2e5580`, the same as the `b_reinforcement` click: opens or closes the reinforcement list | H | the first entry's drop pick: #463 |
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

## Open

- Component 0x218's name and the battle-cam key gates.
- The camera consumer of the tether target [M].
- What the battle-cam cameras frame.
- The Next / Prev filters and order.
