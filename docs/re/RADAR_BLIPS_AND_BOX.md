# Radar story blips, the camera-box blackout and the under-attack ping

Retail FoC x64 `StarWarsG.exe`, read in the Ghidra export and the disassembly (#320, 2026-11-24). Tiers: **[H]** read
directly, **[M]** inferred with good support, **[L]** guess. Addresses are image offsets from 0x140000000.

Follow-up: the under-attack ping and End_Control_Transition, #479.


All addresses are RVAs (image base 0x140000000). HUD = global 0x140b27f60, radar map = 0x140b2bcb0 (its +0 qword is the
radar render object, "R" below).

## 1. Lua Add_Radar_Blip art

**Answer [H]:** retail does not draw a model or a ring. It draws a CommandBarComponents.xml component, `radar_blip`
(component index 0x20f), on the radar, and that component pulses.

- Registration 0x5483fd: vtable 0x8934a0, call slot 10 = 0x6a6ac0 (Remove_Radar_Blip: vtable 0x893540, call 0x6a8bb0).
- 0x6a6ac0, tactical branch: `Add_Radar_Blip(where, name)`. Parameter 1 is any position source (0x4990b0). The object is
  kept only when parameter 1 is a game-object wrapper (0xa43b18); a plain position leaves it null. The id is the CRC32
  of `name` (0x215a30). Call: `0x2de700(HUD, pos, duration -1.0, id, 0, object)`.
  Galactic branch (mode 0): `Add_Radar_Blip(planet)` gives `0x3d01d0(HUD, planet, 1, -1.0)`, which flashes the
  planet's label component (vtable +0x120 with (1, 1, 0.5, 3)). There is no radar quad in galactic mode.
- 0x2de700 creates one record of 0x28 bytes in the HUD +0x2228 array:
  - component instance = `0x34b350(0x347b50(0x20f))`, the `radar_blip` component;
  - +8 the object; +0x10 the duration; +0x14 v = 255.0; +0x18 direction = 0; +0x1c the id;
  - +0x20 the persistent flag, set when the duration is exactly -1.
  - Placement 0x2fccb0: the world point goes through R's world box (0x268f80) into the radar component's screen rect
    (0x2f60a0), then sets the component position (0x352650). The blip is on the radar, not at the unit on screen.
- Art: `<CommandBarComponent Name="radar_blip">` in config.meg CommandBarComponents.xml:
  - Type Button, `Icon_Texture_Name i_radar_focus.tga`, Scale 1.0, Base_Layer 5, No_Shell, no Size, no Color;
  - the MT_CommandBar.mtd entry for `I_RADAR_FOCUS.TGA` is 30x30 at (1625, 226).
  - So the size is 30x30 atlas px times the scale below [M: the size pipeline is the port's `component_art_size`].
- **Pulse [H], 0x315c40, every HUD update (0x301c69, dt in seconds as the radar events use it):**
  - non-persistent records lose dt from their duration; a record whose duration reaches 0 is freed;
  - v ping-pongs between 0 and 255 at 1024/s:
    - falling: `v = clamp(v - 1024 dt, -255, 255)`, and when v <= 0 the direction turns rising and v = -v;
    - rising: `v = clamp(v + 1024 dt, ...)`, and when v >= 255 the direction turns falling;
  - the component (getter 0x34c290 = comp +0x148) gets alpha byte +0x38 = (int)v, and scale +0x30 = v / 512 + 0.5;
  - +0x38 is the alpha: the colour setter 0x351930 writes r/g/b to comp +0x181..+0x183 and a to +0x180. +0x30 is the
    size multiplier (size / 2 * scale, e.g. 0x655440).
  - Period about 0.5 s. The alpha swings 255 to 0 and back, and the size swings 1.0x to 0.5x.
  - The start is 255 and falling.
- The position follows the object every frame (object +0x78/+0x7c). Nothing tests visibility or fog.
- **Removal:**
  - Remove_Radar_Blip (0x6a8bb0) runs `0x2ff240(HUD, crc)` and removes only the first record with that id.
  - Add does not upsert: a second Add of the same name adds a second record.
  - When the game object is removed from the object manager, 0x2a6a00 (at 0x2a6da1) calls 0x2e9d60, which drops every
    record that holds it [M: 0x2a6a00 is the object-removal path].

**Port today:**
- engine/apps/game/include/alamo/game/radar_feed.hpp:18-19 marks the gold ring as a `[port]` stand-in.
- engine/ui/src/tactical_hud.cpp:826-836 draws a 1 s gold ring plus a dot.
- engine/ui/src/hud_radar.cpp:324 `RadarEvents::set_marker` upserts by name, and a dead object's blip is kept.

**Change:**
- `RadarEvent` gains `float v = 255; bool rising = false;`. `RadarEvents::update(dt)` steps the rule above for every
  event that has `marker` set.
- tactical_hud.cpp:829 drops the ring. It draws `add_atlas_quad(dl_, layer_of(radar_blip), Alpha, *atlas_,
  "i_radar_focus.tga", c, component_art_size(radar_blip) * (v / 512 + 0.5), 1.0, Rgba8{255, 255, 255, (uint8_t)v})`,
  with the tint white [M].
- Optional [H]: drop the upsert (append, remove the first match), and drop the blip when the object is removed instead
  of keeping it.
- Test (alamo_ui_hud_tests):
  - a marker after update(0.125) has v = 127 and is falling;
  - a further update(0.25) takes v to 127 - 256 = -129, which reflects to v = 129, rising;
  - the draw uses i_radar_focus.tga at 30 * (v / 512 + 0.5) and alpha (int)v.

## 2. Blackout outside the camera box

**Answer [H]:**
- The test reads R's box, not the camera directly (0x359bc1..0x359bec): centre R+0x88/+0x8c, half extents
  R+0x94/+0x98 (and +0x9c).
- A texel is opaque black (0xff000000) when the half extents are non-zero and the texel's unclamped world point has
  `|x - cx| > hx || |y - cy| > hy`.
- The order is: cell outside grid (transparent) first, then this test, then water and the class rule.
- R's box is filled by the land activation 0x3b7af0 through mode vtable slot 0x2b0 = 0x35f140, before the picture
  build 0x358b00 runs:
  - 0x2cea10 returns camera +0x10F and copies camera +0x110..+0x124 and +0x128..+0x13c;
  - 0x26a640 stores those blocks at R+0x70 and R+0x88;
  - **when +0x10F is 0, 0x35f140 zeroes the half extents first, so nothing is black.**
- With +0x10F set, 0x35f140 also makes R's world rect (0x26a570, R+0x60..+0x6c) a square: centre (cx, cy), half side
  max(hx, hy).
  - That rect is what 0x268f80 / 0x26af80 map through, so radar events and story blips use it [H] (unit blips use it
    too [M]);
  - so on a non-square map the radar shows black strips beside the playable box.
- **Where +0x10F is set:** only 0x2d0350 writes it (= 1), copying (A, B) into camera +0x110 / +0x128. Its callers:
  - 0x3b9653, the land map loader 0x3b8cd0, called with B = land mode +0x418 (centre) / +0x424 (half);
  - 0x4d8694, the space loader, which builds no picture.
- In 0x3b8cd0, B comes from **.ted chunk 0x103 mini-chunks 05 and 06**, two vec3 corners: centre = midpoint, half =
  |difference| / 2.
  - The same box marks cells outside it Obstacle (0x59f110).
  - B is computed only when both corners are non-zero.
  - The 0x2d0350 call itself is unconditional when the mode has a camera, so on land +0x10F is always 1.
  - The box is not chunk 0x10C and not a camera XML. The tested box is the look-at box (+0x128 / +0x134), which equals
    the land playable box.

**Port today:**
- engine/ui/include/alamo/ui/hud_radar.hpp:117 has the `[port] Not applied` note.
- engine/apps/game/app/app_screens.cpp:751 `radar_world_box` and engine/apps/game/app/app_hud.cpp:905-908 use the
  playable rect as is.

**Change:**
- `RadarTerrainSource` gains `bool box = false; float box_cx, box_cy, box_hx, box_hy;`.
- In `radar_terrain_rgba`, after the in-grid check, an outside-box texel (unclamped point, strict `>`) gets {0, 0, 0, 255}.
- `radar_land_terrain` passes the land playable box (`MapData::playable_box`, 0x103 05/06).
- Land `radar_world_box()` and `hf.map_min/max` become the square around the playable box. Space stays as is: no
  picture, and its R rect also goes through 0x35f140, but that is out of scope here [M].
- Tests:
  - (hud) a grid in-range texel outside the box is opaque black; one inside keeps its class colour; an off-grid texel
    stays transparent even outside the box;
  - (game) playable 0..200 x 0..100 gives a world box of [0, 200] x [-50, 150].

## 3. Under-attack ping

**Answer [H]: yes. Retail pings the radar when a local (or allied community-property) base object is attacked.** It
does not use a RADARMAP.XML event.

- The RADARMAP event API is 0x355ca0 (by object, name string) and 0x355b80 (by position). Both reach the core 0x355e00
  with the CRC of the upper-cased name. Every caller:
  - 0x41aa2: GameConstants GUI_Movement_Click / Double_Click / Attack_Movement_Click_Radar_Event_Name
    (0x140b15a10/a30/a50 = +0x210/+0x230/+0x250);
  - 0x3a2585 and 0x61c830: Interdict_Activated;
  - 0x402be5: Galactic_Build_Complete_Planet_Ping;
  - 0x4367e5: the spawned beacon type's Beacon_Radar_Map_Event_Name (+0x1798);
  - 0x501bee: Begin_Control_Transition_Radar_Event (+0x1758);
  - **0x500f30: End_Control_Transition_Radar_Event (+0x1778).** It plays in 0x500e30, the capture-point owner change,
    when the local player is allied with the old or the new owner (0x2823e0, relation 0 [M]). The port now raises it
    from app_hud.cpp when a capture point changes hands (mcx4 t14; checked for #479).
  - 0x58cf16: DEFAULT_UNIT_DIE.
  - None of these is on an attack path.
- The under-attack ping goes through the **HUD blip record 0x2de700**, not a RADARMAP event. Land 0x3ba180 (call
  0x3ba394) and space 0x4d8ae0 (call 0x4d8cf4) handle the combat notification (attacker, victim). This is legacy
  legacy docs/re/AUDIO.md 9.8; its "size 4.0" is wrong, the 4.0 is the duration. The rule:
  - the mode countdown (land +0x414, space +0x3c4) is 0, and the attacker is hostile to the victim's owner (0x39a4e0);
  - the victim is a base object: land behaviour slot +0x391 DUMMY_GROUND_STRUCTURE or +0x394
    DUMMY_LAND_BASE_LEVEL_COMPONENT; space +0x38e DUMMY_STAR_BASE or +0x392 DUMMY_ORBITAL_STRUCTURE;
  - the owner is the local player, or an ally with type +0x27 (Is_Community_Property);
  - then the countdown = FPS * Audio `Delay_Between_{Land,Space}_Base_Attack_Announcement_Seconds` (0x140b30324 /
    0x140b30320 = Audio +0x1c4 / +0x1c0, vanilla 60);
  - **if** the local faction has `SFXEvent_{Land,Space}_Base_Under_Attack_Announcement` (+0x6e0 / +0x6d0), it plays
    (0x2d5290) **and** runs `0x2de700(HUD, victim pos, 4.0, id 0xffffffff, 0, object null)`.
- The result is the same pulsing `radar_blip` (i_radar_focus.tga) as part 1, at the victim's position at hit time. It is
  fixed (it does not follow) and lasts 4.0 s.

**Port (#479):**
- `GameAudio::combat` returns true when the announcement fired: cooldown passed, then the faction tag present. The
  cooldown restarts either way, as retail sets the countdown before the tag test.
- `GameAudioHost::combat` then queues the victim position. The app takes the queue after `battle_tick` and calls
  `RadarEvents::ping(x, y, 4.0)`: an unnamed, non-persistent marker that pulses like a story blip.
- Tests: alamo_game_audio_host_tests and alamo_ui_hud_tests.

**Change (as planned):**
- `GameAudio::combat` (and `GameAudioHost::combat`) report that the announcement fired, with the victim position.
  GameAudioHost already finds the victim in its damage loop (game_audio_host.cpp:1010-1026).
- That path adds a non-persistent blip record: `RadarEvent{marker, persistent false, duration 4.0, v 255}` at the
  victim (x, y), pulsing by the part 1 rule and removed after 4 s.
- Gate it exactly like the SFX: the cooldown has passed **and** the faction has the tag.
- Test (game_session or audio_host): a hostile hit on a local DUMMY_GROUND_STRUCTURE adds one 4 s blip at the victim. A
  second hit inside 60 s adds none. A faction without the announcement tag gets no blip.

## Open

- The default RGB tint of `radar_blip`: the XML has no Color, white assumed [M].
- The exact canvas size of a Size-less Button (30x30 from the atlas) [M].
- The defaults of land mode +0x418/+0x424 when a map lacks 0x103 corners [L]. The vanilla maps have them.
- The meaning of diplomacy relation 0 in 0x2823e0 [M].
