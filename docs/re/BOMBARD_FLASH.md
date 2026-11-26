# The planetary bombardment lighting flash (#314)

Source: the Ghidra export of StarWarsG.exe (x64, RVAs below). Evidence tags as in the other docs/re files.

## Making a light: `0x52a110` (PlanetaryBombardManagerClass update) [H]

For each running bombardment, each shot reads the faction's lists by the shot's salvo index i (the record's +0x2c,
which steps at the end of each salvo):

- Bombardment_Lighting_Color_List (faction +0x27d0, 12-byte entries) entry i, else (0, 0, 0);
- Bombardment_Lighting_Intensity_List (+0x27e8) entry i, else 0;
- Bombardment_Lighting_Seconds_List (+0x2800) entry i.

When i is inside the seconds list and seconds[i] > 0, a 0x3c-byte light record is pushed on the manager's list
(+0x30 / +0x38 / +0x40): life = (int)(FPS × seconds[i]) frames (FPS = `0x140b0a340`), the shot position, the colour and
the intensity, age 0. No light otherwise.

Ghidra types the record as ints. The consumer reads the colour, intensity and position as floats and the life and age
as counts, so the record is read here as floats with integer life and age [M].

## Each frame: `0x52aa50` (the start of `0x52a110`, before that frame's shots) [H]

For each light, at age a of life L:

- envelope e = a / (0.85 L) while a ≤ 0.85 L, else 1 − (a − 0.85 L) / (L − 0.85 L), clamped to [0, 1];
- colour = e × intensity × colour;
- direction: d = (pos.xy − look.xy) × (1 / 1500), where look is the tactical camera's look-at (game mode +0x1e8, the
  camera controller's +0x18 / +0x1c [M]); when |d| ≥ 1 the direction is (d / |d|, 0), else (d, sqrt(1 − |d|²));
- the age steps by one; at age ≥ L the light is freed.

The lights left are summed into an order-2 spherical-harmonics set (`0x2364c0` clears it, `0x236550` adds each light
as a directional light of weight 1), and 0.4 × the sum of their colours is an ambient term. Both go to the scene
(`0x2773e0`: SH at +0xc0..+0x128, ambient at +0x12c..+0x134).

## Applying it: `0x277830` (the scene's environment lighting) [H]

The fill SH is built from the ambient and the two fill lights (`0x236500`, then `0x236550` × 2), and the scene's extra SH
from +0xc0 is added to it (`0x236910`). The extra ambient is added to the ambient colour only while the global
`0x140a12670` (returned by `0x1789f0`) is 0 [M: it looks like the render-path selector].

No option is read on this path: the flash does not depend on DynamicLighting [H].

## The port

- `render::BombardFlashes` (`engine/render/include/alamo/render/bombard_flash.hpp`): the envelope, the direction and
  the ambient share.
- `TacticalBattle::bombard_flashes()`: one presentation event per shot that makes a light (salvo-index lookup, the
  seconds gate), not saved or hashed.
- The battle view steps the lights once per sim tick, after that tick's new shots are added. The battle host reads the
  feed after each tick.
- `Lighting::bombard` / `Lighting::bombard_ambient`: the lights join the fill and full SH, like the lightning flash,
  and the ambient share is added to the frame ambient.
- The ambient gate `0x140a12670` is taken as 0, the default render path [M].
- Only Underworld data sets the lists.
