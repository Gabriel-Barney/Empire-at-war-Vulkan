# Frame effects: wipe, alpha cross-fade, fake motion blur (#66)

Source: the Ghidra export of StarWarsG.exe (x64, RVAs below). Evidence tags as in the other docs/re files.

## The three effects [H]

The renderer's init (`0x17f...`, the shader loads around `Engine\SceneBloom` / `Engine\SceneHeat`) creates:

| Effect | Object | Shader | Globals |
| --- | --- | --- | --- |
| Wipe | `alD3dTransitionEffectLinearWipe` (`0x1b1b10`) | `Engine\FrameEffectWipe` (PrevSceneTexture, WipeFraction, two more) | `0x140a6fdd0` |
| Alpha cross-fade | `alD3dTransitionEffectAlphaFade` (`0x1b20b0`) | `Engine\FrameEffectAlpha` (PrevSceneTexture) | `0x140a6fde0` |
| Fake motion blur | the effect itself | `Engine\FrameEffectFakeMotionBlur` (BlendFactor, RadialBlur, PrevFrameTexture) | `0x140a6eb48` |

One mode word, `0x140a6fe80`, picks the running one: 1 wipe, 2 alpha, 3 fake motion blur, 0 none.

- `0x181af0(mode)` sets the mode and zeroes the alpha fade's clock (+0x38) and the wipe's (+0x2c).
- `0x181c50(dt)` runs the mode once: for 1 / 2 it steps the effect (+0x20), stops it when done (+0x28 true, mode 0),
  else draws it (+0x30). Mode 3 draws the motion blur from PrevFrameTexture and copies the frame again each frame.
- `0x181b30` (the wipe's texture, direction and length) and `0x181bd0` (BlendFactor / RadialBlur) **have no callers**.

## The request path [H]

- `0x25fb10(mode)` stores a pending request (`0x140b0a108`).
- `0x25fb30(dt)` runs from the frame function `0x60330`, before the GUI draw (`0x28c520`): first `0x181c50(dt)`, then a
  pending request copies the frame into PrevSceneTexture (`0x181c20`: `0x198380(rt, 0) == 0`) and, if that worked,
  `0x181af0(mode)`. The request is then cleared either way.
- `0x60330` passes **0.1** to `0x25fb30` [M]: Ghidra types `0x25fb30` as `void(void)`, but the call site passes
  `0x3ccccccd` and the wrapper calls `0x181c50` first, with the register unchanged. The fade's clock therefore steps
  by 0.1 per drawn frame, not by the frame time.

Every caller of `0x25fb10` passes **2**: the alpha cross-fade is the only effect the shipped game requests. The wipe
and the fake motion blur are loaded but never started.

## The alpha cross-fade [H]

- Length: `0x181bb0(0.25)` (`0x1b2250`, +0x34) with every request.
- Step: +0x38 += dt (`0x1b2260`); done when length ≤ t (`0x1b2280`).
- Draw (`0x1b22a0`): one full-screen quad with PrevSceneTexture, diffuse colour white and alpha
  `clamp(1 − t / length, 0, 1) × 255`.
- The blend state lives in the effect file (`FrameEffectAlpha.fx`), which was not read: src-alpha / inverse-src-alpha
  is assumed from the quad's diffuse alpha [M].

So a request made in frame N captures frame N; frames N+1 and N+2 draw the capture over the new view with alpha 0.6
and 0.2; at frame N+3 (t = 0.3) the fade is over.

## Who requests it [H]

All in the tactical camera controller (`TacticalCameraControllerClass`, the overview fields `Tactical_Overview_*`
+0xb0..0xcc of the camera type):

- `0x2ce520` (the zoom notch): the change into overview level 1 or 2 by zooming out at the maximum distance, and the
  change down one level by zooming in (level 2 sets the +0x9c countdown, which ends in level 1; level 1 sets +0xa4,
  which ends in level 0).
- `0x2d07b0` (hotkey slot 76, Overview): 0 → 1 → 2 → 0.
- `0x2ce9c0(cam, fade)`: back to level 0, the fade only when `fade` is true. The land / space mode updates (`0x3bad20`,
  `0x4d9110`) call it with true while the battle cam (`CinematicClass 0x140b27a10`) runs; Esc (`0x61fe0`, key 0x1b)
  calls it with false.
- The frame loop `0x5d990` requests it once when a flag (`0x140b2f1d8`) is set; no writer of that flag was found [L].

The camera change itself waits 2 frames (+0x9c / +0xa0 / +0xa4 count down in the camera update), so the captured
frame shows the old view.

## The port

- `render::FrameFade` (`engine/render/include/alamo/render/frame_fade.hpp`): the request, the 0.1 step, the 0.25 s
  length and the alpha law.
- `Renderer::apply_frame_fade` (`engine/render/src/frame_fade_pass.cpp`, `shaders/frame_fade.hlsl`): the capture into a
  PrevSceneTexture image and the blended full-screen pass.
- The game app requests a fade for each battle-camera overview level change
  (`TacticalCameraController::overview_changes`) and draws it over the battle world before the HUD.
- The camera's zoom-in goes down one level (2 → 1 → 0), as `0x2ce520`.
- The battle cam leaves an overview level with the fade (`0x2ce9c0(cam, 1)` from the mode updates while the cam runs).
- Not ported: the wipe and the fake motion blur (no retail trigger); the `0x140b2f1d8` request [L].
- [port] The camera still blends the overview pose with smoothstep over `overview_blend_time` (0.5 s); retail cuts
  after its 2-frame countdown. The port has no Esc exit from the overview, so every port change fades.
