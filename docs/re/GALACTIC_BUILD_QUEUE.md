# Galactic build queue pads

The galactic command bar's build queue: the domes `queue00`..`queue09` and how a filled one is drawn. Port counterpart:
`GalacticHud::build_bar` (`engine/ui/src/galactic_hud.cpp`).

## The queue tints (Left_Queue_Tint, Right_Queue_Tint) [H] (#727)

- **The fill `0x3e6550`** calls `0x3e6220(hud, list, count, cap, first_component)` twice:
  - with component 0x50 and the space build queue (`list + 0x20`, count `+0x30`);
  - with component 0x55 and the ground build queue (`list + 8`, count `+0x18`).
- **Component ids** (the exe's name table at `0xa174e0 + 0x48 * id`): 0x50..0x54 are `queue00`..`queue04` and
  0x55..0x59 are `queue05`..`queue09`.
- **`0x3e6220`**, for each entry that has a pad:
  - sets the icon (vf +0xa0; `i_button_temporary.tga` when the type has none);
  - then sets the tint (vf +0x10):
    - `DAT_140b16bc4` (GC +0x13c4, `Left_Queue_Tint`) when `first_component <= 0x54`, so the space queue;
    - `DAT_140b16bd4` (GC +0x13d4, `Right_Queue_Tint`) otherwise, so the ground queue;
  - the first entry gets the build dial and its "N%" text.
- **The tint** is four floats; FoC sets `128 128 255 215` (blue) and `255 128 128 215` (red).
- **The flicker [M]:** the same loop writes the tint's alpha × 255 into vf +0x268's byte +0x38, with a random drop of
  25 on some refreshes (`0x1ffb40`). That element is likely the hologram cone. Its draw is not traced.

### Port

- `HudConstants` reads both tags (the last one wins).
- The host copies them into `GalacticHudFrame`.
- `GalacticHud` tints each filled queue pad by its entry's side: space takes the left tint, ground the right one.
- The flicker is not modelled. It is a known gap, tracked on #727.
