# Decisions

The standing product and engineering decisions that bind this repository: scope, platform, dependencies, port features
(camera, controls, graphics, fleets) and build policy. Code cites them by heading, as `docs/DECISIONS.md "Camera and controls"`, so a heading's text
is part of the contract: do not rename a heading that code cites.

Two other documents hold the rest. `docs/MODS.md` holds mod loading and mod compatibility (what a mod may rely on, the
mod-system decisions, the out-of-scope tag-ledger rows). `docs/SIMULATION.md` holds the simulation, determinism and
verification rules (fixed step, CRC, compilers and floating point, parity tiers, outcome tests). Build and test commands
are in README.md.

This file was carried over on 2026-11-21 from the older checkout's `docs/DECISIONS.md` (`docs/LEGACY_DOCS.md` links it). Process and roadmap entries of
that file (branching, agent tooling, roadmap layout and workflow) were not carried over; they are history, not rules.

To add a decision, add a `## Title` section with a unique heading, then one line saying who decided and when, then the rule,
then why. Change or remove a decision only on Gabriel's word, and keep the old heading if code cites it (say what replaced
it). Mark port-only behaviour `[port]` or `[port: Gabriel]` in code and cite the heading here.

## Platform, compilers and scope

Gabriel, 2026-10-27 to 2026-10-29.

- **Windows x64 is the only supported platform.** No other platforms and no non-Windows code paths. The Xlib / XCB /
  Wayland surface code, the GCC build and its toolchain were removed (2026-10-29); GCC is retired, with no GCC-only fixes.
- **MSVC builds what ships**, rendering is **Vulkan** only. Shaders are compiled to SPIR-V by DXC (`toolchain\dxc`), so
  the C++ compiler choice does not affect them. MSVC is the only C++ compiler ("MSVC is the only compiler" below); the
  compiler rules that protect determinism are in `docs/SIMULATION.md`.
- **Single player only.** Multiplayer is out of scope: no networking, lobby, online screens or MP-only work (see
  "Multiplayer code removed").
- **The original game's save files are not loaded.** The port uses only its own save format (N1); do not let the
  original's save layout shape the port's state.
- **Lua 5.0.2**, the original source, vendored and pinned (`toolchain\7_lua.cmd`). It keeps its 5.0 semantics: no 5.1+
  upgrade.
- **Parity target: base Forces of Corruption** is the content the port is measured against (Gabriel, 2026-10-28). How
  parity is judged (by behaviour within tolerance, not by matching retail's CRCs) is in `docs/SIMULATION.md`; the mod
  compatibility goal is in `docs/MODS.md`.
- RenderDoc is out of scope (its failed runs are not fixed); GPU captures use apitrace.

## Multiplayer code removed

Gabriel, 2026-10-29 (scope) and 2026-11-07 (CU7, removal).

The code once kept for multiplayer is removed, not kept on disk: the bit-packed network wire codec, the network /
lockstep scheduler paths (receive from peers, command turns, MaxAhead, FrameSync retuning, late-command bumping), the
EventBus parallel lanes, the multiplayer lobby screens, the MP-only auto-resolve dialog with its Withdraw action, and the
tactical fog overrun reveal of networked game types (`FogOfWar::service_overrun` stays with its tests). Online screens
(buddy, clan, validation, internet login) and network-only command details are out of scope too.

Layouts did not change: the `.acmd` header and the N1 saves still carry the late / mode / turn / max_ahead bytes, written
as the constants 0 / 0 / 1 / 2 and checked on read, so goldens and saves stay byte-identical.

## MSVC is the only compiler

Gabriel, 2026-11-21, following `docs/GROUND_UP_WORKFLOW_DESIGN.md`. Replaces "Clang runs nightly only" (same day),
which kept clang 20 as a nightly second compiler.

- **No clang and no GCC anywhere.** No clang, clang-cl, clangd, clang-format or clang-tidy: no clang build, nightly
  job, lint hook, clang sanitizer job or MSVC-versus-clang comparison, and no `__clang__` / `__GNUC__` branches in
  project code. Vendored sources keep their own.
- **What covers clang's checks.** Per change: `engine/CMakeLists.txt` turns on off-by-default MSVC warnings for the
  conversion family (C4365, C4388, C5219, C4242, C4254, C4826, C4800, C4287, C4296, C4265, C4062) in C++ engine code.
  Nightly, and before a high-risk change lands: MSVC `/analyze`, an MSVC AddressSanitizer build, Debug and optimized
  Release runs, and the determinism checks in `docs/SIMULATION.md`. `.github/workflows/nightly.yml` runs
  `dev.cmd check --preset analyze`, which fails on an `/analyze` finding not in `tests/analyze_baseline.txt`; the issue
  "Nightly checks: last passing" holds the dated last-passing commit. Before a high-risk change lands, run that check
  locally; `landing` does not require it yet (Gabriel, 2026-11-21, #10 decision 3). `/analyze` findings are not
  compiler errors (`/analyze:WX-`): the first full run had 319 (241 of them C6262 stack size, mostly in tests), so the
  baseline holds them as debt to work down and only a new finding fails. Third-party code is out of scope through
  `/analyze:external-` and the repository-path filter. The same workflow runs `dev.cmd check --preset asan`: the
  synthetic suite built with `/fsanitize=address` and PDBs, without the tests labelled `no_asan` (deliberate crashes
  that ASan would intercept); any ASan report fails it. Open on 2026-11-22: #24, so that leg fails until it is fixed.
- **Limits.** Compiler portability is not promised. Undefined behaviour and races are caught only by the MSVC checks,
  the tests and review, so code that converts out-of-range values keeps using the defined helpers
  (`alamo/core/x86_int.hpp`, `alamo/core/frames.hpp`).
- **Old comments.** Comments that record a past measurement on clang ("measured on MSVC and clang") are history and
  may stay. A golden written by MSVC is the only reference.
- Why: clang took about 28 % of recorded test time and about half of a sim change's landing check; in about 1,000 clang
  builds it failed where MSVC passed 14 times, mostly flakes. One pinned toolchain is simpler to install, cache and
  diagnose.

## Approved third-party dependencies

Gabriel, 2026-10-27 onward. `engine/` takes no third-party dependency without Gabriel's OK. Each approved one is pinned
by exact version and SHA-256 with a setup step under `toolchain\` (`toolchain\2_setup.cmd`).

- **Lua VM:** Lua 5.0.2 (see the scope section).
- **Audio + video:** miniaudio for audio (`toolchain\8_miniaudio.cmd`); FFmpeg's Bink decoder for the game's Bink
  video. FFmpeg 8.1.3 is built by `toolchain\9_ffmpeg_bink.cmd` as an LGPL v2.1-or-later shared build with only the
  `bink` / `binkaudio_rdft` / `binkaudio_dct` decoders and the `bink` demuxer (no gpl, version3 or nonfree parts).
  Licence mode is dynamic linking: `engine/video` loads the unmodified DLLs at run time (no import libs), they ship beside
  the exe with `COPYING.LGPLv2.1`, and a user can drop in their own build.
- **Particles:** a GPU particle system (a design choice, no library).
- **SMAA:** the reference SMAA (MIT), vendored unmodified in `engine/render/third_party/smaa/` (Gabriel, 2026-11-13).
- **FSR and XeSS:** see "FSR upscaling" and "XeSS upscaling".
- **Dropped:** no binary XML cache and no Lua bytecode cache (Gabriel, 2026-10-30): scripts and XML load from source.
  No Tracy; no AVX2 dispatch (baseline SSE2, no CPU-feature dispatch).

## Command Center launcher

Gabriel, 2026-11-13.

- The Command Center launcher (Rust, egui) lives in `launcher/`, beside `engine/`, never linking engine code. It starts
  `alamo_game.exe` with profiles, mods, Workshop items, saves, graphics options and diagnostics. Build and test it with
  `dev.cmd launcher` / `dev.cmd launcher-test` (README.md); it is not part of the engine build.
- The engine's dependency rule covers `engine/` only, but the launcher's crates are pinned by `launcher/Cargo.lock`, and
  a new direct crate needs Gabriel's OK like an engine dependency. Rust 1.100.0 via rustup-init 1.29.3 (pinned
  SHA-256) in `toolchain\rust`, set up by `toolchain\12_rust.cmd`, linking with the project-local MSVC.
- **No Valve code in the launcher.** No `steamworks` crate and no `steam_api64.dll` (its SDK agreement is for Steamworks
  partners). Subscriptions, install state and download progress come from Steam's files on this PC
  (`appworkshop_32470.acf`, `workshop\content\32470`, `workshop\downloads\32470`); item details from Steam's public web
  API (`GetPublishedFileDetails`, no key). Subscribe, unsubscribe and update open the item in the Steam client
  (`steam://`) and the launcher watches the files for the result. In-launcher Workshop search needs the user's own Steam
  Web API key (`QueryFiles` refuses without one); without a key, Browse opens the search in the Steam client.

## Camera and controls

Gabriel, 2026-10-27 (later rows dated). Port features beyond the original, binding for the cameras, bindings, HUDs and
the game host; marked `[port: Gabriel]` in code.

- **Pause:** a pause button on the galactic and the tactical level, plus a rebindable pause hotkey. Pausing stops sim
  ticks and goes through the command stream (port event 63, subtype `kPortPauseSubtype` "PA"), so replays stay valid: a
  pause changes when ticks run, never what they compute.
- **Camera FOV:** adjustable field of view for the galactic and the tactical camera, separate settings. Presentation
  only, never read by the sim.
- **Tactical camera rotation:** a full 360 degrees, free on yaw, pitch and roll with no clamps (`free_rotation`).
- **Movement keys:** camera movement defaults to W A S D in both modes (rebindable). W, A, S and D are used only for
  camera movement: any default hotkey on them, with or without Ctrl / Shift / Alt, moves to another key. The arrows freed
  by this drive yaw / pitch, Numpad 4 / 6 roll.
- **Queueable unit actions:** Shift+click queues an order after the unit's current ones; a plain click clears the queue.
  Each queued order is a command in the stream with a "queued" flag; the queue lives in the sim, so it is deterministic
  and replays. The HUD draws each unit's queue as a dotted path with numbered markers.
- **Mission holocron opens at campaign start** (2026-11-13): a new galactic conquest (not a loaded save, skirmish or the
  attract demo) opens the mission holocron by itself once its first story chapter is added and no other story dialog,
  cinematic or battle prompt is up; a campaign with no chapter in its first 300 ticks never opens it. Retail only flashes
  the holocron button. Presentation only.
- **Squadron wings** (2026-11-13): in tactical battles the local player's fighter squadrons form one wing and its bomber
  squadrons another. Buttons at the left edge (FIGHTERS n / BOMBERS n) and F2 / F3 select a whole wing, Shift adds it,
  Ctrl centres the camera only, a second press within 0.4 s centres the camera; whole squadrons up to the 127-unit cap.
  (The squadron-of-12 resize of the same day was removed on 2026-11-17; squadrons are retail's, see `docs/MODS.md`.)
- Fleet and ship names, fleet cohesion and fleet groups are their own sections below.

## Fleet and ship names

Gabriel, 2026-11-13. `[port: Gabriel]`; retail has no such names.

The player can name and rename their own galactic fleets and individual ships (any unit in one of their fleets or on a
planet's surface). A double click on an own fleet-organiser pad (fleet header or unit pad) opens a rename prompt: up to
32 characters (`kMaxObjectNameChars`), Enter saves, Esc cancels, an empty name restores the default. The rename is a
port-only command in the stream (PortExtension subtype `RN`, `kRenameSubtype`) applied by `GalacticSim::rename`. A name
belongs to its object: a ship keeps it across fleets, landings and battles; a merged-away fleet loses it. Names are
presentation only: saved (`galactic.state` v7; v6 saves load with no names) but not hashed, so CRCs and goldens do not
change. They show as organiser-pad tooltips; a named ship gets its own pad instead of joining its type's stack. Not yet:
names on the galaxy map's fleet buttons and in tactical battles.

## Fleet cohesion

Gabriel, 2026-11-13, built as a port feature (not retail).

A per-player Loose / Tight setting for space battles. Loose is the default and retail behaviour (each ship flies at its
own speed), and every AI, Lua and script move is Loose. Tight: the player's own UI moves, queued ones too, cap every
non-fighter ship at the order's slowest such ship's max speed (`MoveOrder::cohesion_tight`); fighters and a lone ship are
not capped; a stop or a later loose order clears it. Toggled by **O** (hotkey slot 162 `FleetCohesion`) through the port
command subtype `kPortFleetCohesionSubtype` ("FC"), so replays carry it; the battle HUD shows "Fleet cohesion: Loose /
Tight (O)". The preference lasts across the battles of a session, not across sessions. Loose runs hash as before. Land
ignores it (land group moves already share the slowest member's speed). Not done: speed-factor effects on the rest of the
fleet, slot-keeping waits for slow-turning ships.

## Fleet groups: exception to the current focus

Gabriel, 2026-11-13. A named exception to "Current focus: parity tiers 1 and 2 only".

Fleet groups (Total War: Warhammer 3 style persistent unit groups in tactical battles) are built although they are not
retail behaviour. Design: the claude.ai doc "Fleet groups design" (2026-11-13). Rules:

- **Off by default** (`[port]` option; `alamo_game --fleet-groups`, `SessionSpec::fleet_groups`,
  `TacticalBattle::enable_fleet_groups`). Off, no group can exist: every tier-1 rule, tier-2 distribution and golden CRC
  is unchanged. With it on, the save carries the `battle.fleet_groups` chunk in every battle, and group state enters
  the tick CRC only once a group exists; a save made with it on loads only in a session with it on. The player turns it
  on with "Fleet Groups (new games)" in the port's Camera and Controls dialog (profile key `fleet_groups`, 2026-11-22):
  it applies to games started afterwards, while a game in progress and a loaded save keep the setting they started
  with. `--fleet-groups` turns it on whatever the option says. (The retail Gameplay dialog takes no port controls.)
- Groups are shown inside the retail selected-unit bar (a framed run of portraits per group), not as a separate strip. Layout (2026-11-22):
  groups first in creation order, each a framed run of per-type portraits under a header tab (number, name, a lock
  when locked; a tab click selects the whole group), then the ungrouped units by type; past the last slot one "+N"
  slot holds and selects the rest.
- A **locked** group keeps its members' offsets, moves at its slowest member's speed **and waits for stragglers**
  (leaders slow down when a member falls behind).
- Fleet cohesion stays separate: a locked group's move is one single-ship order per member, which Tight cohesion leaves
  uncapped, so the group's own cap applies.
- Merge and split (2026-11-22): a group's tab in the bar shows Merge when the selection spans two or more groups (the
  groups fold into the oldest, which keeps its id, name and lock; slots are recaptured; at most 40 members) and Split
  when the group is only partly selected (a new group of the selected units). A right click on a tab renames the group.
- World visuals (2026-11-22), presentation only, drawn with the unit selection circles (no new art): a selected locked
  group gets one ground outline enclosing its members in the player's selection colour, and while a ground move is
  aimed (order button held) a faint circle shows at each member's destination slot, turned with the drag's facing; the
  slots come from the same plan the order uses (`TacticalBattle::fleet_move_preview`).
- Hotkeys N (new group), U (ungroup), Y (lock / unlock), slots 163..165. Every fleet group order is one PortExtension
  event (id 63) of subtype `kFleetGroupSubtype`, so the original event catalogue is unchanged.

## Current focus: parity tiers 1 and 2 only

Gabriel, 2026-11-10, until he lifts it.

Work goes to tier-1 and tier-2 parity (tiers defined in `docs/SIMULATION.md`). Tier-3 work (internals, performance,
port-only features) is not started unless a section here names it as an exception, as "Fleet groups: exception to the
current focus", "FSR upscaling" and "XeSS upscaling" do. A crash, hang or save corruption is always fixed regardless of
tier. Work without a tier (tools, harness, UI) is done when it serves tier-1 or tier-2 work. Lifting the focus is
Gabriel's call.

## Graphics and display

Gabriel, 2026-10-28 (later rows dated). Port features beyond the original. Presentation only, never read by the sim;
marked `[port: Gabriel]` in code. Each is a Video or Advanced video option the player can turn off.

- **Refresh-rate aware:** views interpolate between the last two sim snapshots (render alpha from tick timing), so
  motion is smooth at any refresh rate instead of stepping at 30 Hz.
- **V-sync mode:** FIFO (standard) or Mailbox (low latency); V-sync off = Immediate. Mailbox falls back to FIFO where the
  device lacks it, and the Video dialog shows the mode in use.
- **Frame rate cap:** Off, the display's refresh rate, or 30 to 240 fps; works with V-sync on.
- **Anti-aliasing list** (2026-11-13): the original's D3D9 back-buffer MSAA was never ported; the list is one exclusive
  choice of Off, SMAA, TAA, MSAA 2x, MSAA 4x, MSAA 8x, one fixed level each. The renderer keeps `RecordOptions::taa`,
  `smaa` and `msaa` independent so they can stack through options and `alamo_render` flags. TAA uses Halton(2,3)
  jitter, depth reprojection with per-object motion vectors (`Instance::motion_id`), and depth-based disocclusion
  rejection; skeleton animation is not in the motion vectors. Alpha-tested foliage gets no MSAA smoothing.
- **Ambient occlusion:** half-resolution screen-space AO from depth, land battles only.
- **Anisotropic filtering:** Off, 2x, 4x, 8x, 16x (clamped to the device) for every linear-filtered world texture.
- **Display Mode list** (2026-11-13): replaces "Run in window". **Fullscreen** switches the monitor to the picked mode
  and takes exclusive Vulkan full screen (`VK_EXT_full_screen_exclusive`; alt-tab minimizes and restores the desktop
  resolution; without the extension a frameless window over the switched monitor). **Borderless fullscreen** keeps the
  desktop resolution. **Windowed** is a framed window. Profile key `window_mode=fullscreen|borderless|windowed` (old
  `windowed=0|1` still loads).
- **UI scale** (2026-11-13): 100 % to 500 %; at scale s one HUD canvas unit is s x 1440 / 768 physical pixels, so 100 %
  is the original's size on a 1440p screen. Past the original's fit-the-screen size only text keeps growing (at most
  4 / 3). The game starts at 100 % whatever Windows' display scale is; scripted, smoke and benchmark runs keep the
  original fitted UI. Profile key `ui_scale`.
- **Galactic map Hor+** (2026-11-13): the original's galactic horizontal FOV, atan(0.5), is taken as the 4:3 value;
  wider windows keep 4:3's vertical FOV with no aspect cap. The tactical camera keeps the original's Hor+ rule capped at
  16:9.
- **Removed options** (2026-11-13): Auto Detect (thresholds were a guess), Hardware Mouse (the game always draws its own
  cursor) and Widescreen (the resolution list shows every mode and the HUD follows the window's shape). Old
  `hardware_mouse=` / `widescreen=` lines are ignored.

## FSR upscaling

Gabriel, 2026-11-13 ("Approve FidelityFX SDK"), lifting the tier-3 pause for this one feature.

AMD FidelityFX / FSR SDK 2.4.0 is an approved dependency. Only the Vulkan upscaler is used: the C API headers and the two
AMD-signed DLLs `amd_fidelityfx_loader_vk.dll` and `amd_fidelityfx_upscaler_vk.dll` from the v2.4.0 release commit
(`7539ae8b`), pinned by SHA-256 in `toolchain\11_fidelityfx.cmd` (MIT). The DLLs are loaded at run time and copied
beside the exes (`alamo_render_copy_ffx`); without the headers the renderer builds without FSR. A missing DLL, a device
or driver without FSR, or a failed context creation turns FSR off, and the frame is drawn at full resolution with TAA.
The runtime picks FSR 4.1 (ML) where the GPU supports it and FSR 3.1 otherwise. FSR replaces the TAA resolve for its
frames and reuses TAA's jitter and motion bookkeeping. Presentation only, never read by the sim.

## XeSS upscaling

Gabriel, 2026-11-13 ("Approve, start XeSS"), lifting the tier-3 pause for this one feature, as for FSR.

Intel XeSS SDK 3.0.2 is an approved dependency. Only XeSS Super Resolution on Vulkan is used (no frame generation, no
XeLL): the headers `xess.h` / `xess_vk.h` and the Intel-signed `libxess.dll` from the v3.0.2 release commit
(`8fe81bdb`), pinned by SHA-256 in `toolchain\13_xess.cmd`. Binary-only under the Intel Simplified Software License:
the unmodified DLL may be redistributed with its notice (copied beside the exes as `intel_xess_license.txt`); no reverse
engineering or modification. Loaded at run time; any failure turns XeSS off and the frame falls back to TAA.
`rhi::ContextDesc::upscaler_features` also enables `VK_EXT_mutable_descriptor_type` for it. Five list rows (Native AA,
Quality 1.7x, Balanced 2x, Performance 2.3x, Ultra Performance 3x) sit next to the FSR rows; the render size is the
runtime's `xessGetOptimalInputResolution`. `[port]` choices: the same inputs and jitter sequence as FSR, and the
display-referred 8-bit colour passed as LDR input (exposure 1), since the frame graph has no HDR scene colour.

## Side goals deferred

Gabriel, 2026-11-06.

These side goals are off the plan until the core game and mod compatibility are complete: first-launch shader
precompile, loading and build speed (per-map preload, LTO / PGO, a performance regression gate), CPU efficiency (job
system rework, scratch arenas, parallel AI scoring, a C-runtime math ban in sim folders), GPU efficiency (half-resolution
particles, cheaper water) and graphics fidelity. Why: performance goals were already met with wide margins (worst tick
p99 2.2 ms against a 16.7 ms budget). It is a deferral, not a ban: no one starts any of it without a new decision here.
The definitions are in `legacy docs/DEFERRED_SIDE_GOALS.md`. The repeatable benchmark side goal (fixed saves, recorded
orders, scripted camera, per-tick CRCs; `tools/benchmark`) is not deferred. Parallel AI scoring that is part of the
parallel sim redesign (P0-P7, "Performance goal") is excepted from this deferral (Gabriel, 2026-11-22): the later focus
exception there wins.

## Front-end and skirmish choices

- **New Campaign by data** (lane-3 run, 2026-11-25, #452): New Campaign opens FoC's `IDD_MAIN_CAMPAIGN_DIALOG`, with the
  first-click tutorial question, only when the data has both that dialog and the campaign its Start plays
  (`Full_Story_Campaign_Underworld`). Other data keeps the story-set list `IDD_CAMPAIGN_SELECTION_DIALOG`, with no question.
  This covers EaW, whose GUIDialogs.rc also has an `IDD_MAIN_CAMPAIGN_DIALOG`, and mods. EaW's own New Campaign handler is
  not reverse-engineered, and the list starts every story campaign the data has. Esc on the question closes it, and the
  tutorial list it opens goes back to the main menu as it does from there; retail's answers for both are not traced.
- **EaW main-menu attract demo** (Gabriel, 2026-10-28): the port does not replay `.rec` recordings. For EaW's
  `Front_end_demo_attract.rec` it reads only the map path from the header and runs the same live AI-vs-AI battle as the
  FoC attract path. Replay would need tick-exact parity with the EaW exe and an undecoded command format.
- **Pre-battle skirmish buy screen kept** (Gabriel, 2026-11-04): the port keeps its buy-units screen between skirmish
  staging and Start Game, although retail single-player skirmish has none (its buy dialog opens only inside a battle).
  Its list follows retail's filter. Revisit if the in-battle buy dialog is built.
- **Screenshot files** (lane-3 run, 2026-11-24, #466): the screenshot key keeps retail's name
  `EAW_<YYYYMMDD>_<HHMMSScc>` and `ScreenShots` folder, but writes a PNG (the port's capture path already encodes PNG;
  retail writes TGA) into a folder beside the saves, not under the game's working folder (the port's executable folder
  can be read-only).

## Mod picker load report stays in-process

Gabriel, 2026-11-23 standing rule for the three-lane issue run (take the recommended option and record why), applied
2026-11-24; #136.

The mod picker's load report (`App::check_mod`, `app_mods.cpp`) keeps running `game::check_picked_mod` on a worker thread
inside `alamo_game`. It is not moved to an `alamo_mod_check --json` child process.

Why:

- **Memory:** a child process does not lower what the machine uses. The checker's second copy of the mod's data exists
  either way; only the process that holds it changes, and a child adds its own start-up baseline. The issue measured
  Thrawn's Revenge at 576 MB peak working set (modc t38 b). A standalone `alamo_mod_check --no-dxc` run, without the
  runtime DXC compile as in the picker, measured on 2026-11-24 gives 447 MB for the base game (1 s) and 888 MB for
  Thrawn's Revenge (281 s). That figure is the whole checker process, start-up included. The game's own peak is not
  the limit: x64 has no address-space pressure, and the copy is freed when the report ends.
- **Cost:** a child process needs a JSON reader for the whole `ModCheckReport`, kept in step with the writer.
  `alamo_mod_check` would have to ship next to `alamo_game` and be found at run time, and the game would need
  process-spawn and timeout plumbing.
- **Responsiveness:** the menu already stays responsive, because the check runs on a worker thread.

Revisit if a checker crash ever takes the game down (a child process would isolate it), or if the picker must cancel a
long check. Today a running check is not cancelled; the newest request waits for it to finish.

## Headless and AI defaults

- **Game-object Lua scripts run in base FoC** (Gabriel, 2026-11-13, replacing the 2026-11-04 mod-only rule): a retail
  probe showed vanilla objects run their `Lua_Script`, so every `GameSession` runs them and `alamo_headless` runs them by
  default; `--no-object-scripts` turns them off.
- **Headless GameScoring stays opt-in** (Gabriel, 2026-11-04): `alamo_headless` runs the GameScoring script only with
  `--scoring`; `alamo_game` always runs it, as the game does. Without it the headless CRC goldens stay fixed.
- **Galactic AI stagger stays off** (Gabriel, 2026-11-12): every player's galactic AI clocks run on the same frames, as
  retail (`GalacticAiConfig::stagger = false`). A per-player phase moved random-driven outcomes out of retail's range.
  `alamo_headless --ai-stagger` keeps it as a benchmark knob.
- **Tactical / skirmish AI matches galactic** (Gabriel, 2026-11-12): the tactical and skirmish AI drop the per-player
  phase by default too (`TacticalAiConfig::stagger`, `TacticalAiSetup::stagger` false) and use retail's cadence with the
  galactic player count. The scheduling rules are in `docs/SIMULATION.md` "AI scheduling".

## Performance goal

Gabriel, 2026-10-30; later amended by the AI stagger rows above.

Use the cores better and keep large game or AI updates off single ticks (budget 33 ms per tick at 30 ticks/s): measure
first, then move long AI and pathing work to background jobs applied a fixed number of ticks later, parallelise serial
per-object passes, budget Lua GC. Results must stay identical for any worker count (`docs/SIMULATION.md`). The parallel
sim redesign (`docs/SIMULATION.md` "Parallel simulation design"; steps P0-P7, an exception to the current focus since 2026-11-14)
lands every step that moves AI decisions or random draws behind a config flag defaulting to the old path; the default is
switched in its own commit only after the parity harness shows the new path inside the old path's distribution.

## Build speed

Gabriel, 2026-11-13 and 2026-11-19.

The shared PCH (`engine/pch`) holds std headers only and defaults ON only when no compiler cache is active (sccache
cannot cache `/Fp` compiles). Hosted CI builds through sccache with the PCH off (2026-11-21), except a pull request with no compiler cache to
restore, which builds with the PCH and without sccache: a cold sccache build took ~37 min, the PCH build ~22 min
(2026-11-21). Rejected as no gain: lld-link, removing `-U ALAMO_ENGINE_MODULES`. A header diet beyond
the bo3 split is out of scope: not to be re-added without a new decision here.

## One development entry point

Gabriel, 2026-11-21.

`dev.cmd` at the repository root is the only development command, for people, agents and CI alike. It stays a thin
shim: it checks that `toolchain\venv\Scripts\python.exe` exists (printing the remediation if not) and passes every
argument unchanged to `tools/dev.py`. The logic (doctor, check presets, suite, package, reports) lives in Python, not
in batch, and there is no `dev.ps1`. This replaces the PowerShell entry point proposed in
`docs/GROUND_UP_WORKFLOW_DESIGN.md`.

Why: the rest of the automation (`tools/test_suite.py`, the lints, the gate checks) is already Python, so the entry
point can share its code and test itself with `unittest`. Every caller (cmd, PowerShell, Git Bash, GitHub Actions,
Claude Code, Codex) runs the same command with exact argument lists and no execution-policy setup. Batch keeps only
what it does best: running on a bare machine and reporting a missing Python.

## Retail and GPU evidence is a recorded local run

Gabriel, 2026-11-21.

The GitHub repository is public. Hosted CI runs the synthetic suite only, and no self-hosted runner executes
pull-request code. Game data, retail captures and private mods never reach hosted CI. A change that needs the retail,
vulkan or traces suite (the evidence table of `docs/GROUND_UP_WORKFLOW_DESIGN.md`) is checked on the maintainer's
Windows machine with `dev.cmd suite <name>` at the pull request's head commit. The pull request records that commit,
the suite, and the counts and skip reasons from `build\test-results\report-<suite>.md`.

Why: a self-hosted runner on a public repository would run untrusted pull-request code on the machine that holds the
game install, and the retail data may not be copied to hosted runners.

## Codex skills are generated, not acceptance-tested

Gabriel, 2026-11-21.

`tools/skills.py` keeps generating `.agents/skills/` for Codex from the same source as `.claude/skills/`, and
`alamo_skills_check` keeps checking both trees. The workflow is acceptance-tested in Claude Code only: no sample issue
is run in Codex, and no cross-client resume (one client plans, the other implements) is required. When Codex comes into
use, run one user-invoked and one model-invoked skill in it first and check for name clashes.

Why: Codex is not installed on the development machine, and the cross-client proofs cost more than they buy for one
maintainer working in Claude Code.

## Decisions live in DECISIONS.md

Gabriel, 2026-11-21.

The architecture decision records (ADRs) of `docs/GROUND_UP_WORKFLOW_DESIGN.md` are sections of this file, written as
its introduction says; there is no `docs/adr/` folder. Simulation and mod rules stay in `docs/SIMULATION.md` and
`docs/MODS.md`. Terms go in `GLOSSARY.md`, created with the workflow skills.

Why: code already cites these headings and `tools/lint/check_doc_refs.py` checks them, so one file keeps every
decision findable by its heading.

## Simulation changes are declared

Gabriel, 2026-11-21 (#10).

A pull request that changes a path listed in `tests/simulation_paths.txt` has a `Behaviour:` line in its description,
`none` or the scenarios and systems whose behaviour changes, and `landing` fails one without it
(`docs/SIMULATION.md` "Declared behaviour changes"). This replaces the `Behaviour:` trailers on the head commit that
`docs/GROUND_UP_WORKFLOW_DESIGN.md` first proposed. The list is the definition of simulation code; a pull request that
moves code into or out of the simulation updates it.

Why: pull requests land squashed, and the squashed commit takes the pull request's title and description, not the head
commit's trailers. The description is where the declaration survives and where a reviewer reads it. A checked-in path
list can be checked against the diff; the issue's Risk field cannot.

## Pull requests land squashed without an up-to-date rule

Gabriel, 2026-11-21.

The `main` ruleset requires `landing` to pass but not that the branch be up to date with `main`. Pull requests land
by squash only, with linear history, and the branch is deleted after the merge. A break between two changes that
landed close together shows up in the `landing` run on the push to `main` and is fixed by a follow-up pull request.

Why: with the up-to-date rule, every landing forced a merge of `main` into each other open pull request and another
full `landing` run. The pull request run already tests the merge with `main` as it was then, so for one maintainer
the extra merges cost more than the rare break they catch. A merge queue would cover both, but GitHub offers it only
to repositories owned by an organization.

## Big-file splits are by position, with named-namespace helpers

Gabriel, 2026-11-13 (roadmap bo3).

A very large `.cpp` is split into `<stem>.cpp` plus `<stem>_partN.cpp` and a shared header, in the same CMake target.
The file's anonymous namespace becomes the named namespace `bo3split` in that header (helpers `inline`), so every part
shares one definition. Test executables keep one `main`, their test names and `add_test` entries; the parts move only
`Class::member` and external function definitions. Behaviour, goldens and CRCs do not change.

## alamo_game runs at above-normal process priority

Gabriel, 2026-11-14 (asked for better frame times at high worker counts).

`[port]` `alamo_game` sets `ABOVE_NORMAL_PRIORITY_CLASS` at start (`set_process_priority`,
`engine/apps/game/app/game_display.cpp`). `--priority normal` keeps it normal; `--smoke` runs stay normal unless
`--priority above` is given. Why: on a loaded CPU every fork-join of the sim and renderer waits for the worker the OS
scheduled last; above-normal priority halved the loaded frame p99. Scheduling only: nothing reads it, results and CRCs
do not change. `alamo_headless` and the test executables stay at normal priority.

## fxc ships in test builds

Gabriel, 2026-11-16.

`fxc.exe` and `d3dcompiler_47.dll` (Windows SDK) may ship in test builds, which stay private to Gabriel. With them,
`alamo_game` and `alamo_mod_check` run the `.fx` prebuild by themselves when a mod is mounted; without them the fallback
still works and the report says the source is not compiled. A public release needs this asked again. (The same entry's
one-off permission for Claude to drive `alamo_game` was for two runs on 2026-11-16 only and is not carried over.)

## Release symbols

Gabriel, 2026-11-15.

Crash minidumps need the PDB of the exact build that crashed. `ALAMO_RELEASE_PDB` (MSVC `/Z7` plus
`/DEBUG /OPT:REF /OPT:ICF`) stays off by default in every build tree; test builds turn it on and keep the symbols on the
developer machine, not in the player's package. The release workflow's draft GitHub release may carry the build's PDBs
as a separate `-pdb` zip beside the package, so crash dumps from that test build can be read; they never go inside the
package or onto a published release (Gabriel, 2026-11-22).

## The roadmap is the issue tracker

Gabriel, 2026-11-22 ("Import everything"; GitHub Pages for the roadmap page).

All open work of the old roadmap store became GitHub issues (#44-#163), and the store is no longer updated. The roadmap
page (README.md "Roadmap") draws the flowchart from `docs/roadmap/flow.json` and reads the issues live, joining them to
boxes through `roadmap:<box>` labels; it is published with GitHub Pages. Importing an item does not put it in focus:
"Current focus: parity tiers 1 and 2 only" still decides what is worked on.

## Each new game draws its own seed

Gabriel, 2026-11-22 (#192, relayed by the #58 planning session).

Without `--seed`, alamo_game seeds each new front-end campaign or skirmish from the wall clock and logs the seed
(`session: seed N (replay with --seed N)`), as retail seeds the synced RNG per session. `--seed N` gives every new game N.
`--smoke` and benchmark runs keep their fixed seed, and so does the attract demo (the run's seed plus its start count). A
story LOAD_CAMPAIGN reuses the running game's seed, and a loaded save keeps the seed it was saved with. The clock is read
once by the host before the session starts, so this is an input to the session, not a wall-clock value the sim sees
(`docs/SIMULATION.md` "Scheduling by tick number"). Why: identical choices should not replay identical AI, combat and
auto-resolve outcomes, while tests and replays stay deterministic.

## AI and player orders stay separate

Gabriel, 2026-11-24 (#407, split from #147).

The tactical AI keeps its own order semantics in `TacticalBattle::ai_order`. AI moves, attacks and Lua MoveTo /
AttackMove / Guard are not merged into the player order path (`order_move` / `order_attack` / `order_move_to_object`).
Both already enter through `TacticalBattle::apply_order` (#147).

Why: the rules `ai_order` keeps are RE-cited as AI-specific (Move_Internal flags 0 gives a kind-1 snapshot; one land
attack order per target group; the task force as one group order; Attack_Move on a non-hostile object; the task-force
Order_Attack refusal drop), and there is no [M] or better evidence that retail's AI orders behave like player command
events. Merging would replace cited AI behaviour with the player path's. Revisit only with such evidence.
