# Galactic pause: object animation

## Animate_During_Galactic_Mode_Pause [H] (#727)

- **The object animation loop `0x2be640`** runs every frame over the mode's objects (list at +0xf0), calling `0x3a6b80(object, dt, paused)` for each one.
- **"Paused"** means mode type 0 (galactic, vf +0xe0) with the pause counter `DAT_140b1540c` > 0.
- **While paused**, the loop animates the objects only when `Animate_During_Galactic_Mode_Pause` (GC +0x11e6c, `DAT_140b2766c`) is set. FoC sets `no`, so the galactic objects freeze while paused.
- **`0x4b2c00`** reads the same flag. When the mode reports paused (vf +0x48) and the flag is clear, it passes 0 as its time step to `0x362c60`.

## Port

- The galactic idle clips run on the galactic mode frame (`GalacticFrame::anim_frames`), which stops with the sim, so FoC's freeze is the default.
- **The pause** (`GalacticView::set_paused`) is set when either of these holds:
  - the mode's pause count, `GalacticSim::paused()` (the battle banner, a story block), during which the mode frame stops;
  - the port's session pause, which stops the ticks.
- **`render::GalacticPauseClock`** reads the flag. With the flag set:
  - it gives `GalacticFrame::pause_anim_frames`: 30 frames per second of wall time while paused;
  - it keeps that offset afterwards, so the clips do not jump back. It lives as long as the `GalacticView`;
  - the objects' clips (planets, the selection model) are posed at `anim_frames + pause_anim_frames`;
  - the route sparks stay on `anim_frames`, since they are not objects of the loop.
- **`0x4b2c00`'s dt 0** while paused with the flag clear is the default as well: nothing advances the mode frame then.
