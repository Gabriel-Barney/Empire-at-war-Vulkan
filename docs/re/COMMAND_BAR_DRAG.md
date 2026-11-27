# Command-bar drag: snapping and stacks

These notes cover retail FoC x64 `StarWarsG.exe`, read in the Ghidra export and the disassembly (#722, 2026-11-27).
- **Tiers:** **[H]** read directly, **[M]** inferred with good support, **[L]** guess.
- **Addresses** are image offsets from 0x140000000.
- **Component data:** a component's CommandBarComponent data hangs at component +0x80. Its flag bytes +0x3e0..+0x3e7 are
  all 0 in the data constructor 0x2d9890, so an absent tag is false [H].

| Tag | Flag | Reader |
|---|---|---|
| `Stackable` | +0x3e1 bit 0x10 | 0x34cef0 (no data: false) |
| `Snap_Drag` | +0x3e3 bit 0x01 | inline in 0x352650 |
| `Snap_Location` | +0x3e3 bit 0x02 | inline in 0x352650 |
| `Can_Drag_Stack` | +0x3e4 bit 0x02 | 0x34b330 (no data: true) |

## Snapping (Snap_Drag, Snap_Location) [H]

`0x352650(icon, position, target, allow_snap)` moves a drag icon to `position` and clears the icon's "snapped" bit
(+0xc0 bit 1). It snaps when all of these hold:
- the icon's component has `Snap_Drag`;
- `target` is non-null and its component has `Snap_Location`;
- `allow_snap` is set.

The position then becomes the target's bone (+0xa0) projected to the screen, and the snapped bit is set.

The command bar's mouse move 0x2f6c30 calls it while a pad is dragged (+0x2160):
- the lead icon: target = the component under the cursor (+0x21a0), allow_snap 1;
- every other icon of the drag group (+0x2170, count +0x2178): allow_snap 0, at the lead icon's snapped position when
  the lead snapped.

The other 30 callers pass no target, so they never snap.

**Base FoC:** `Snap_Drag` and `Snap_Location` are both Yes on `Pad_00`..`Pad_09` and on every `q1_`/`q2_`/`q3_`
organiser pad (115 rows each).

**Port:** `GalacticHud::drag_icon_position`. The port's placement centre of a component is its bone position
(`ComponentPlacement::center`).

## Ctrl-drag of a stack (Stackable, Can_Drag_Stack)

These rows are not ported yet (a follow-up of #722).

**Ctrl state.** HUD +0x21a9 is Ctrl held [H]:
- it is set each frame from `GetKeyState(VK_CONTROL)` (0x20e2b0, in 0x30c3b0);
- it is cleared on the Ctrl key-up (0x2f6bc0, key 0x11; Shift 0x10 clears +0x21aa).

**Stackable** [H]. The organiser drop 0x3d0790 takes (HUD, target pad, dragged pad, ...) and logs
"Item %s dropped in slot %d". It checks `Stackable` on the **dragged** pad:
- With Ctrl held and that pad Stackable, the whole stack moves. 0x2f6270 gives the stack's units, and 0x3e0eb0 moves
  them.
- Otherwise a one-entry list moves (the one unit).

Base FoC: Yes on 142 of 145 rows. The No rows are the header pads `q1_0000`, `q2_0000` and `q3_0000`.

**Can_Drag_Stack** [H for the flag, M for its look]:
- In 0x2f6c30 and 0x2f6bc0, the dragged pad gets virtual slot +0x190 with (Ctrl held && `Can_Drag_Stack`).
- That slot does nothing (0x1fa90) for every component class except `TextButtonComponentClass` (vtable 0x864180 →
  0x354760). There it stores the flag at +0x7dd.
- The TextButton draw 0x34f160 reads +0x7dd in its draw state 5. With +0x7dd clear and the pad Stackable, it skips
  the state textures. +0x7dd also gates a bone-follow block.
- Base FoC sets `Can_Drag_Stack` only on the tactical reinforcement slots `r_0000`..`r_0403`, all No. The galactic
  pads leave it absent (false), so with base data +0x7dd is never set.
