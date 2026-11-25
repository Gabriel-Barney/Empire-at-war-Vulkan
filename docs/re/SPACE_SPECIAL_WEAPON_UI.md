# Space special weapon: the human UI path

Retail FoC x64 `StarWarsG.exe`, read in the Ghidra export and the disassembly (#431, 2026-11-24). Tiers: **[H]** read
directly, **[M]** inferred with good support, **[L]** guess. Addresses are image offsets from 0x140000000.

Behaviours: 0x2f SPECIAL_WEAPON, 0x37 LOBBING_SUPERWEAPON, 0x5c DUMMY_STAR_BASE (a station). A weapon's aim flag is
byte +0x28 of its weapon state (object +0x158).

## Starting the aim [H]

- **The button** `b_special_weapon` (click `0x346a80`) reads the weapon object that the HUD update stored on it. Then
  `0x31f760` does one of three things:
  - **SPECIAL_WEAPON:** `0x491b40` toggles the aim flag. A set flag is cleared, which cancels the aim. Otherwise, if
    another special weapon of the owner is aimed, it refuses with the negative sound; else it sets the flag. It does not
    test readiness.
  - **LOBBING_SUPERWEAPON:** the flag is set only when it is 0. There is no toggle.
  - **Station:** `0x387d20(hp, 1)` sets the hard point's aim again. There is no toggle, so a second press keeps it.
- **Hotkey 22 in space** (`0x63f00` case 0x16) goes over the local player's three special-weapon slots. It sends the
  first SPECIAL_WEAPON with readiness >= 1 to `0x491b40`.
  - It never reaches a station.
  - It does not test SpecialWeaponsAllowed.

## The pointer while aiming [H]

`0x2ad280` sets the cursor action to `0xc + Special_Weapon_Target_Action_Index`, with a negative index giving 0xc and
the result capped at 0xe. A station uses 0xc. The actions map to the pointers:
- `POINTER_ATTACK_WITH_SPECIAL_WEAPON_0` / `_1` / `_2`;
- `_INVALID` for action 0xf.

Stock data: the Ion Cannon (index 1) shows `_1`; the Hypervelocity Gun (0) and stations show `_0`.

**Target test `0x491f80`.** The target must:
- have a type;
- not have `+0x3a0 & 0x40`;
- not be allied with the weapon's owner;
- have a category in `Special_Weapon_Valid_Targets`.

A failed test gives action 0xf.

## The click [H, M where marked]

- **A valid target.** The click queues command 25 (`0x40a130` / `0x40a650`) with these fields:
  - weapon;
  - target object;
  - `at_position` 0;
  - player = the weapon's owner.

  It then clears the aim flag locally. No sound plays.
- **Execute `0x40a3b0`.** It gates on SpecialWeaponsAllowed and on readiness >= 0.99, then fires through `0x491cd0`. It
  does not call `0x491b40`. It runs the destroy-after sweep whatever the fire returns. The port does the same (#460),
  and the cursor and the click test the target with `TacticalBattle::sw_can_target`.
- **An invalid target** (action 0xf) queues no command, plays no sound and keeps the aim [M].
- **A right click** while aiming cancels every aim [M].

## The button, `0x317d00` [H]

- **Never hidden.** It only sets disabled, the weapon, the dial and the icon.
- **Disabled with a full dial (1.0)** when any of these holds:
  - there is no weapon and no station;
  - SpecialWeaponsAllowed is false;
  - controls are locked;
  - the gate `0x28aa00` / `0x28a890` fails [L: its meaning].
- **Otherwise:**
  - **disabled** while the readiness is below 1;
  - **dial** = the readiness, with 0 shown as 1.0.
- **Icon:** by the local faction's name, compared with three HUD strings: 0, 1, 2, or 3 for any other faction
  [M: Rebel, Empire, Underworld].

## Port

- **Fire:** command 25 already matched (`app_input.cpp`).
- **What #431 changes:**
  - the aim pointer comes from `Special_Weapon_Target_Action_Index` (station 0) through
    `game::special_weapon_pointer_index`;
  - the button is never hidden, with the full dial and the faction icon as above (`game::space_weapon_button`,
    `game::space_weapon_faction_icon`);
  - an invalid click is silent;
  - a second button press keeps a station aim;
  - the hotkey skips stations and does not test SpecialWeaponsAllowed.
- **Still `[port]`:**
  - The valid-target test is `is_enemy` until #460 adds `0x491f80`'s test.
  - One aimed weapon at a time.

## Open

- The meaning of the `0x317d00` gate (`0x28aa00` / `0x28a890`) [L].
- Which mouse path reaches the cancel-all (`0x325ab`) [M].
- The tooltip index rule (`0x2e32f0`).
