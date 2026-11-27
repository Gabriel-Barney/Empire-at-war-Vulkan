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
| `Scale_Duration` | +0x3a8 (float) | 0x350df0, 0x34ff70 |
| `Cross_Fade` | +0x3e2 bit 0x10 | 0x352d80, 0x350360 |

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

**Ctrl state.** HUD +0x21a9 is Ctrl held [H]:
- it is set each frame from `GetKeyState(VK_CONTROL)` (0x20e2b0, in 0x30c3b0);
- it is cleared on the Ctrl key-up (0x2f6bc0, key 0x11; Shift 0x10 clears +0x21aa).

**Stackable** [H]. The organiser drop 0x3d0790 takes (HUD, target pad, dragged pad, ...) and logs
"Item %s dropped in slot %d". It checks `Stackable` on the **dragged** pad:
- With Ctrl held and that pad Stackable, the whole stack moves. 0x2f6270 gives the dragged unit's organiser type
  (0x3973b0), and 0x3e0eb0(hud, fleet, type, slot) moves every unit of that type in the fleet: a slot event
  (0x5b06b0) where needed, then the unit's organiser slot (0x3a87c0).
- Otherwise a one-entry list moves (the one unit).
- Into another fleet's pane, the per-unit fleet commands (0x5afb60) go through the stack's list when the dragged pad is
  Stackable, else the one unit. Within its own pane the drop re-orders the type into the target slot.

Base FoC: Yes on 142 of 145 rows. The No rows are the header pads `q1_0000`, `q2_0000` and `q3_0000`.

**Can_Drag_Stack** [H]:
- In 0x2f6c30 and 0x2f6bc0, the dragged pad gets virtual slot +0x190 with (Ctrl held && `Can_Drag_Stack`).
- That slot does nothing (0x1fa90) for every component class except `TextButtonComponentClass` (vtable 0x864180 →
  0x354760). There it stores the flag at +0x7dd.
- 0x34f160 is the TextButton's **text** draw (its two strings, +0x668 and +0x6b8, through the font manager 0x30adb0).
  In draw state 5 (dragged), with `Should_Render_At_Drag_Pos` set, the text sits at the component's own position
  (+0x30 / +0x34, through 0x261a70; [M] the drag moves it with the cursor) plus the pad's offsets. In state 5 the text
  layer is min(layer + 3, 6) either way.
  - With +0x7dd clear and the pad `Stackable`, the text is skipped. This is the look only: the drop moves the stack on
    Ctrl && `Stackable` alone, so a Ctrl drop of a pad without `Can_Drag_Stack` moves the stack with no count shown.
  - Otherwise the text is drawn there.
  - Without `Should_Render_At_Drag_Pos` the text stays on the pad. (The drag-position path is also taken in any state
    when data +0x3e2 bit 0x80 is set; not ported.)
- The ghost (below) is drawn only while +0x7dd is clear.
- This corrects an earlier reading of this section ("skips the state textures"): what is skipped is the text.
- Base FoC sets `Can_Drag_Stack` only on the tactical reinforcement slots `r_0000`..`r_0403`, all No. The galactic
  pads leave it absent (false), so with base data +0x7dd is never set; and no organiser pad has
  `Should_Render_At_Drag_Pos`, so the text rule shows only with mod data.

**Port** (#763):
- `GalacticHud::drop`: with Ctrl held at the drop (`update_input`'s `ctrl`, as +0x21a9) and the dragged pad
  `Stackable`, `GalacticOrganiserDrag::stack` holds the pad's units (`PadSlot::select`). The merge, launch and land rows
  of `galactic_organiser_drop`, and the fleet-slot rows onto an own fleet (`galactic_slot_drop`), then send their command
  once per unit, in stack order.
- [port] The port's organiser has no per-unit slots (its pads are type stacks), so the in-pane re-order has nothing to
  apply to. The detach, planet-move and fleet-slot rows keep the one unit: retail's per-unit detach goes through op 4,
  whose server side is not traced.
- [port] The Modern organiser (`ModernGalacticHud`) has no CommandBarComponent pads, so it sends the one unit.
- [port] Retail drags a group of icons (+0x2170, see "Snapping"); the port draws the lead icon only, a whole-stack drag
  included. Not ported.
- `GalacticDrag::whole_stack` is +0x7dd. The drag icon draws the pad's text by the rule above, and a
  `Should_Render_At_Drag_Pos` pad's own text leaves the pad while it is dragged. [port] It is drawn on the drag icon's
  layer, not the pad's layer + 3, on one line. `ghost_dragged_pad` makes no ghost while +0x7dd is set.

## Ghosting (Should_Ghost, Ghost_Base_Only) [H]

The TextButton's drag start 0x352880 puts the pad in draw state 5 (+0x5f0). It then makes a ghost of the pad
(+0x600, a new TextButton) when all of these hold:
- the pad's data has `Should_Ghost` (+0x3e2 bit 0x08);
- the data has `Should_Render_At_Drag_Pos`: +0x3e5 bit 0x80, tested as the byte's sign (`cmp byte [data+0x3e5], 0`,
  `jge` = no ghost);
- the pad's stack count (+0x7d8) is at least 2, or the data has `Ghost_Base_Only` (+0x3e3 bit 0x08).

The ghost copies the pad's textures and colours, and its count is the pad's count - 1.
- **`Ghost_Base_Only`:** the ghost's +0x270 bit 1 is cleared, so it draws without the unit's icon and with no count
  [M for which texture slot that bit is]. The ghost flag +0x7de is set.
- **Otherwise:** the ghost's text is `x%d` (0x804eb0) of its count when that count is above 1, else a blank. The ghost
  flag +0x7de is set.
- **No ghost** (a single unit without `Ghost_Base_Only`): +0x7de stays 0.

The TextButton draw 0x34f160 draws the ghost in the pad's place while +0x7de is set (with `Should_Ghost`, and +0x7dd
clear).

**Base FoC:**
- `Should_Ghost` is True on 198 TextButtons: `Pad_NN`, `build_padNN`, `land_fleet_NN`, the `qN_NNNN` organiser pads,
  `r_NNNN` and `unit_padNN`.
- `Ghost_Base_Only` is True on 56 of them: `Pad_NN`, `build_padNN`, `unit_padNN` and the header pads `qN_0000`.

- `Should_Render_At_Drag_Pos` is set on only 20 rows, `r_NNNN`, all False. No ghosting pad has it, so **base FoC never
  makes a ghost**. A mod that sets it on a pad gets one.
- This reading is also the retail reader of `Should_Render_At_Drag_Pos`. It was listed as "without a known retail reader"
  (legacy MCX4_T15); the TextButton draw 0x34f160 reads it too, in draw state 5.

**Port:** `GalacticHud::ghost_dragged_pad`.
- It covers the organiser pads, the land-force pads and the planet-map pads. While such a pad is dragged it shows its
  ghost.
- Where retail makes no ghost, the pad draws as before. The text at the drag position is the Can_Drag_Stack rule
  above (#763).

## The pop-in and the cross-fade (Scale_Duration, Cross_Fade) [H] (#767)

**The scale-up.** `0x350df0` is the component's "activate scale up", a virtual: TextButton vtable 0x864180, slot
+0x118; two more component vtables hold it (pointers at 0x863968 and 0x863c78).
- It sets the timer +0x244 to `Scale_Duration`, sets +0x270 bit 0x10 and +0x240 = 0.
- A duration of 0 logs "Tried to activate scale up on component %s which doesn't have duration specified" and does
  nothing.

**The animation.** The update `0x34ff70` runs while bit 0x10 is set:
- the timer counts down by the frame time;
- while it is above 0, the draw scale (+0x178) is (1 − timer / `Scale_Duration`) × `Scale` (+0x398), so the pad grows
  from nothing to its `Scale`;
- at 0 the bit clears and the scale is `Scale`. There is no shrink.

**The trigger.** The pad fill `0x3095c0(hud, pad, object, ...)` calls slot +0x118 when the object's GUI record
(obj +0x1b0) has +0x30 set, then clears +0x30. The drops set +0x30 on each unit they move:
- `0x3cf7d0` ("drag_land_to_space"), before each unit's fleet command `0x5afb60`;
- the organiser drop paths around `0x3d0790` [M for which row each writer is].

So a unit a drag moved pops in at its new pad. Base FoC: 115 pads set `Scale_Duration`, all 0.125 s (`Pad_NN`, the
organiser and map pads).

**Cross_Fade.** When a `Cross_Fade` component's texture changes (`0x352d80`), a 0.1 s timer starts and the old texture
goes to the second slot. The update `0x350360` fades the old one out and the new one in over that time, on the alpha
byte or on the colour channels. Base FoC: Yes only on `Text_Allegiance_A` (an Icon: the planet's owner emblem).

**Port:**
- `ButtonRuntime::scale_up_start`: `HudButtons::draw` scales the pad by elapsed / `Scale_Duration`, clamped to 1.
  [port] (tier 1, presentation) The offsets and the text are not scaled.
- `GalacticHud` keeps the units an organiser or land-force drop moved (with Ctrl, the stack). The first pad build that
  shows one away from its source fleet starts its scale-up; a unit joining a stack pops that stack's pad. [port]
  (tier 1, presentation) The port builds the pads every frame, so "the next fill" is the first one after the sim
  applied the move; an entry left unseen for 5 s goes; map-pad drags do not pop; any organiser drop that sends a
  command records its units, where retail's writers per row are [M].
- `HudButtons::draw` keeps each `Cross_Fade` component's last icon. On a change the old icon fades out and the new one
  in over `kCrossFadeSeconds` (0.1), on the alpha byte [M: retail picks the alpha or the colour channels]. [port] (tier 1)
  The port sees the change at the draw, not at the texture set: the first icon drawn never fades, and a change made
  while the component is hidden fades at its next show.
