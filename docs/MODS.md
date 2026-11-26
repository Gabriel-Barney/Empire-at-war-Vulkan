# Mods

How the port loads Forces of Corruption mods: the rule that keeps `engine/` free of mod-specific code, how mods are
found and their files resolved, which data mechanisms a mod may use, what the loader reports, and how compatibility is
measured. Build and test commands are in `README.md`. Determinism, CRC and parallel-sim rules are in
`docs/SIMULATION.md`; mod data obeys them exactly as base data does. Other product decisions are in `docs/DECISIONS.md`.

Retail reverse-engineering notes and other documents that are not in this repository are cited as `legacy docs/...`;
`docs/LEGACY_DOCS.md` links each one at the old checkout's pinned commit.

## The agnostic-loading rule

A mod works in the port when it uses the same data mechanisms as base Forces of Corruption. Thrawn's Revenge was the
first and hardest test case; it is a test case, not a target the engine is shaped around.

1. **No mod-specific code paths in `engine/`.** No mod name, workshop id, or faction, unit, planet, campaign or map name
   changes behaviour, and there is no branch of the form "if this is mod X". Behaviour comes from the data files through
   the mechanisms below.
2. **No fixed faction lists or player counts.** Factions come from `FactionFiles.xml` in file order; players come from
   the campaign, the skirmish setup or the map. The only fixed limits are those the original executable has, cited from
   a `docs/re` spec (see "Limits policy").
3. **Names the original itself hard-codes are allowed**, with a comment citing the spec and address (a GameConstants
   field, a Lua global, an engine effect name). A base-game content name (`Empire`, `Rebel`, `TIE_Fighter`, `Kuat`, ...)
   never qualifies unless the spec shows the executable looks it up.
4. **Mechanism, not identity.** A port rule may depend on "a mod layer is loaded", "the faction has no
   `Space_Skirmish_AI_Default_Forces`" or "the type has no `Hyperspace`", never on "the faction is called Empire" or
   "the mod folder is 1125571106".
5. **Base FoC does not move.** Loader, Lua and mod fixes leave base-FoC behaviour unchanged: the base-FoC goldens stay
   byte-identical, and with no mod loaded the port behaves exactly as base FoC. A golden move is a retail question
   first and then needs Gabriel's approval.
6. **Fixes are general.** A gap a mod exposes is fixed as a general mechanism (a tag the loader did not read, a Lua
   function not bound, a fallback the original has). A fix that only makes sense for one mod is not made; the gap is
   reported.

Mod and faction names may appear in tests and their fixtures, docs, `tools/probe_mod/`, and comments that cite where a
behaviour was observed. `tools/lint/check_mod_names.py` enforces rule 1 on the sim modules, the game host
(`engine/apps/game/app`) and `engine/ui`: string literals containing a name from `tools/lint/mod_names.txt` or a
workshop id fail unless marked `[name-ok: reason]`.

## What a mod is

A mod is a folder with a `Data\` tree laid out like FoC's `corruption\Data\`. Anything under that `Data\` replaces or
adds to the game's files by path. A mod needs nothing else: no manifest, no registration, no port-specific file. A
folder without `Data\` mounts nothing and the VFS warns `mod path has no Data folder`. A mod runs on top of an installed
FoC, which in turn needs EaW's `GameData` beside it, as in retail. The port only reads; it never writes into a mod
folder.

## Finding mods and resolving files

### Command line switches

The port takes the original's switches (legacy `docs/re/PHASE2_ANSWERS.md` §14). Keys are case-insensitive and values
may be quoted; `alamo_game`, `alamo_headless` and `alamo_mod_check` pass every `KEY=value` argument to
`vfs::parse_command_line`.

| Switch | Meaning |
|---|---|
| `MODPATH=<dir>` | Adds `<dir>` to the mod search list. A relative path resolves against the FoC folder (`MODPATH=Mods\<Name>` finds `corruption\Mods\<Name>`); an absolute path, including a workshop item folder, works too |
| `STEAMMOD=<id>` | Adds workshop item `<id>` at its command-line position. Not found: `STEAMMOD=<id>: workshop item not found; nothing mounted` (a `Vfs` built without `VfsConfig::steam_resolver` says `no workshop resolver`) |
| `FALLBACKPATH=<dir>` | Adds `<dir>` to the fallback chain, below the MEGs and above EaW `GameData` |

`MODPATH=` and `STEAMMOD=` go into one search list in command-line order (retail mounts each as it parses it, so they
interleave). The earliest entry wins. A submod is just an earlier entry. The game never reorders or completes a command
line; only the tools that build one resolve a stack first ("Stack resolution").

### Steam library discovery

The port does not use the Steam API (`engine/vfs/include/alamo/vfs/steam_workshop.hpp`). A workshop item counts as
installed when `<library>\steamapps\workshop\content\32470\<id>\` exists (32470 is the FoC app id; an id that is not
all digits is never found). Libraries, first match wins: the library holding the FoC folder (the parent of its nearest
`steamapps` ancestor), then for each Steam client folder the folder itself and every `path` of its
`steamapps\libraryfolders.vdf` and `config\libraryfolders.vdf` (file order; the pre-2021 numeric-key form too). Client
folders: `ALAMO_STEAM_ROOT` (`;`-separated; when set, nothing else), else the registry's `SteamPath` / `InstallPath`
(read only), else the default Steam install folder. Local mods are the subfolders of `<FoC>\Mods\` that have `Data\`.

### Mod descriptors and modinfo.json

Every installed mod gets one `vfs::ModDescriptor` per (folder, variant), built before anything is read from it
(`engine/vfs/include/alamo/vfs/mod_registry.hpp`): kind (workshop / `Mods\` / path), id (workshop id or folder name),
name, version, summary, icon, languages, dependencies, Steam game tag, and a content hash (FNV-1a 64 over the sorted
relative path and size of every file under `Data`, plus each MEG's header and tables; write times are not hashed, so a
copied or re-downloaded mod with the same bytes keeps its hash, and a same-size edit is not seen). `modinfo.json`
follows the AlamoEngine-Tools `eaw.modinfo` spec and is read leniently: comments, trailing commas, a missing comma
(warning), and `<variant>-modinfo.json` files (one descriptor each, the main file's values as defaults). Missing `languages` means
English; `description` stands in for a missing `summary`. A malformed file is a warning and the folder name is used.

Retail never opens `modinfo.json`, so nothing in gameplay reads a descriptor: they feed names, languages, dependencies,
reports, the picker and saves. Only the mod catalog (`engine/vfs/src/mod_registry.cpp`) reads the file;
`tools/lint/check_mod_system.py` (ctest `alamo_core_mod_system_lint`) enforces that.

### Stack resolution

Before launch, the tools (`vfs::resolve`, `alamo_mod_check --resolve`, the in-game picker, `alamo_play`, the Command
Center) turn the picked mods into a stack:

1. Start from the picked mods, in the user's order.
2. Add each mod's declared dependencies (breadth-first, the spec's layouts `ResolveRecursive` / `ResolveLastItem` /
   `FullResolved`); for a mod that declares none, add the submod hint's likely parent (`game::submod_hint`: the
   installed mods that have the `Data\XML` files its own list files name and no layer has).
3. A cycle is an error naming it; a parent that is not installed is a warning; two total conversions (each with its own
   `GameObjectFiles.xml` and `FactionFiles.xml`, neither depending on the other) are a warning.
4. Order: every mod before the mods it depends on (submod first); ties keep the order found.
5. Emit the launch arguments and the stack fingerprint (FNV-1a 64 over the ordered content hashes).

`alamo_mod_check --resolve <mod args>` checks the resolved stack (the report's `stack` section); `--resolve-only`
prints it and stops. When a parent is missing the game still starts if the user insists, as retail does. Downloading a
parent is outside the port.

### Base-EaW mods

A mod whose `modinfo.json` Steam tags say `EAW` and not `FOC` is refused before anything loads: retail runs it with
`sweaw.exe`, and the port supports FoC mods only. `alamo_game`, `alamo_headless` and the picker refuse it; the report
sets `base_eaw` and grade G0. A mod without tags whose own `GameObjectFiles.xml` names none of the files FoC's list has
and EaW's lacks is only a warning (`base_eaw_suspect`), so a real FoC mod is never refused on a guess.

### One catalog and one resolver

Every launch surface reads the same data and starts the same list:

- **Install locations** (`vfs::InstallLocations`): the FoC folder, EaW `GameData` beside it, the Steam roots and
  libraries, the user's named folders (`game_path.txt`, `ALAMO_FOC_DATA`) and extra mod folders.
- **Catalog** (`vfs::ModCatalog`): one row per (folder, variant). The catalog key is unique across kinds
  (`1125571106`, `2083545253:subA`, `Mods\AOW`, or an extra folder's host path); `ModDescriptor::key()` is the key of
  save headers and the corpus. A folder with only variant files shows one row per variant. Order: workshop items by
  numeric id, then `Mods\` folders by name, then extra folders. Folders compare by `lexical_folder_key` (normalised, any
  case or separator) and `folder_key` (also follows junctions, symbolic links and `subst` drives). A workshop item and
  a `Mods\` folder that a junction makes one folder keep two rows, so saved arguments keep their meaning.
- **Launch list** (`vfs::ModStack::launch_args`): the resolved stack's arguments, each folder once; empty for a refused
  base-EaW stack. [port] A cycle still launches in its fallback order with the error shown, and an argument naming no
  installed mod is left out with an error: retail would mount either command line, so the port refuses no more than
  retail. The picker, `alamo_play` and `alamo_mod_check --resolve` give the same list.
- **Resolver library**: `game::resolve_mod_stack`, `launch_args_for` and `play_launch_args` live in `alamo_mod_resolve`
  (`mod_resolve.hpp`), which needs only the VFS and the XML list reader. The resolver also hints a parent for a stack
  whose data does not load; the load report's `submod` section gives the same answer for stacks that load.
- **Command Center** (`launcher/`): reads only `alamo_mod_check` JSON. `catalog.rs` runs `--list` (rows gained
  `dependency_roots` and `modinfo_numbers` for the mod hunt), `conflicts.rs` runs `--conflicts`, `stack.rs` runs
  `--resolve`. The launcher keeps no mod scanner, Steam walk or file-resolution rule of its own; `unitdb.rs` keeps its
  own XML / MEG readers only for the Units page and definition counts. Without `alamo_mod_check.exe` next to the game
  or the launcher nothing is detected and a toast says why.

### File resolution order

From `engine/vfs/include/alamo/vfs/vfs.hpp`. First match wins:

1. **Mod search list**, earliest entry first: `<mod>\Data\<rest>` (only paths starting with `Data\`).
2. **FoC loose** `corruption\Data\<rest>`.
3. **MEG archives**, newest-mounted first: FoC `64Patch` > `patch2` > `Patch` > FoC `MegaFiles.xml` entries
   (later-listed wins) > EaW `64Patch` > `patch2` > `Patch` > EaW `MegaFiles.xml` entries.
4. **Fallback chain**: `FALLBACKPATH=` folders in order, then EaW `GameData\Data`.

Consequences:

- **Whole files.** A mod replaces whole files, never parts; two copies are never merged. Shipping
  `GameObjectFiles.xml` replaces the whole list.
- **Loose beats archived.** A mod's loose files beat every MEG, `64Patch.meg` included.
- **Mod archives.** `Data\MegaFiles.xml` and `Data\Patch*.meg` are opened through the search list: a mod's
  `MegaFiles.xml` replaces FoC's list, and a mod MEG with a base MEG's name is mounted instead of it. EaW-side archives
  always come from EaW.
- **Paths** are ASCII case-insensitive and `/` equals `\`.
- **MEG lookup.** Inside one MEG, retail binary-searches the record table by the CRC of the normalised name and never
  compares the stored name (legacy `docs/re/MCX3_MEG_LOOKUP.md`). `meg::Archive::lookup` does the same, so a malformed
  mod MEG loses what retail loses: entries an unsorted table hides, all but one record per CRC, a record whose CRC is
  another name's. Each case is a VFS warning and a retail-equivalent issue (`meg_unsorted`, `meg_duplicate`,
  `meg_name_mismatch`, `meg_index`). Encrypted MEGs stay a warning.
- **Directory scans are not lookups.** Listings (skirmish maps, AI plan folders, scripts) follow the spec that defines
  them; mod-folder maps go to the custom map list.

### Layers and provenance

- **Every resolution names one layer.** `Vfs::layers()` lists them in the order above; `Vfs::layer_of(resolution)`
  gives a `vfs::Layer`: kind, index within the kind, host root, and owning mod (`Layer::mod`: the position in the
  search list, or -1 for the base game).
- **A MEG belongs to the folder it was opened from.** A MEG a mod's `MegaFiles.xml` names, or a mod's `Patch*.meg`, is
  that mod's; a base archive a mod's list names stays base-owned.
- **Ask the layer, not the path.** "Is this file from a mod, and which one" reads `Layer::mod`, never a comparison of
  host paths. No code outside `engine/vfs` calls `Vfs::mod_dirs()` (`check_mod_system.py`, which also refuses the
  removed names `list_installed_mods`, `resolve_stack`, `scan_installed_mods`, `ModRegistry`, `ResolvedStack`).
- **One walk for every copy.** `Vfs::resolve_all(path)` returns every copy in resolution order (`[0]` is what `read`
  returns); code that needs lower copies (the SFX archives, a GUI file's next copy) takes them from there.
- **Findings carry their layer.** `Vfs::create` takes an optional `IssueSink`; each VFS finding goes there with
  `Layer::issue_layer()` (the owning mod's index, `kBaseLayer`, or `kNoLayer`) as well as into `warnings()`.

### One mod environment per process

`vfs::ModEnvironment::open(game, args, issues, error)` (`engine/vfs/include/alamo/vfs/mod_environment.hpp`) is the one
object that opens a game's files: the mount plan from the command line (nothing reordered or added), the `Vfs`, one
`ModDescriptor` per mounted mod (`mounted()`, search-list order, so `Layer::mod` indexes it), the base-EaW refusal
(`refused()`: the environment stays open so a report can name the mod; a host that runs the game stops) and
`owner(layer)` / `owner_of(path)`. `alamo_game`, `alamo_headless`, `alamo_mod_check` and `alamo_session_run` open
through it. The data, text and GUI load once per process and retail takes mods only on its command line, so switching
mods needs a new process.

### What a save remembers

A save stores its VFS command line (`SessionDescriptor::data_args`), a fingerprint of the loaded definitions
(`data_fingerprint`) and, when a mod is loaded, the stack's mods (`SessionDescriptor::mods`: key, name, version, content
hash; a base-game save keeps its earlier bytes). Loading rebuilds the session from those arguments; different data is
refused with the first difference, named per mod: "updated since this save (version a -> b)", "changed since this
save", "was in this save and is not loaded now", "is loaded now and was not in this save"
(`engine/apps/sim/include/alamo/sim/save_session.hpp`).

**Load anyway.** When only the data differs, `alamo_game` offers "Load anyway" and says that continuing an outdated save
on a newer mod version is not supported. Every chunk must still read (the object type list, the save's factions and
campaign, the story's plot scripts); a failure discards the session. A session loaded this way is marked
(`GameSession::loaded_with_different_data`, a log line and a screen line); bugs in it are not port bugs. Saves of the
original game are out of scope.

## Mechanisms a mod may use

Everything is reached through the VFS, so a mod uses a mechanism by shipping the file. The port implements each as the
original does, for base FoC and mods alike.

| Mechanism | Files | Rules |
|---|---|---|
| Factions | `FactionFiles.xml` and the files it names | Appended in file order, upper-cased, first definition of a name wins; a type with empty `Affiliation` gets the first `Is_Neutral` faction; map owners are indices in this order |
| AI player type | Faction `Basic_AI`; campaign `AI_Player_Control`; `Data\XML\AI\` | Skirmish slot: the faction's `Basic_AI`. Campaign: its pair; no pair = no AI. An undefined type = no AI ("Unrecognized AI player type") |
| Skirmish starting forces | `Space_` / `Land_Skirmish_AI_Default_Forces` | From the data for every faction; none or undefined names: the [port] fallback of up to three cheapest buildable units (`data_fallback_forces`) |
| Object types | `GameObjectFiles.xml` and the files it names | One list, no EaW/FoC merge, list order, first definition wins, `Variant_Of_Existing_Type`, unknown list names dropped |
| Galaxy and campaigns | `TradeRouteFiles.xml`, `CampaignFiles.xml` and what they name | One `CampaignFiles.xml`; lists de-duplicated and parsed in ascending CRC32 of the name; last definition wins as a whole |
| Story and Lua | Story XML, `Data\Scripts\` under the fixed roots (`AI\`, `Interventions\`, `Evaluators\`, `GameObject\`, `Story\`, `Library\`, `FreeStore\`, `Miscellaneous\`, ...) | Looked up by name (`.lua`, then `.lc`); a loose mod script overrides an archived one; Lua 5.0.2; object scripts via `Lua_Script` |
| AI data | `Data\XML\AI\`, `Data\Scripts\AI\` | Templates, goals, equations, difficulty, plans |
| GameConstants and boot databases | `GameConstants.xml` and the boot list (SFX, music, speech, abilities, hard points, movement classes, ...) | Loaded once at startup |
| Maps | `Data\Art\Maps\*.ted` | Official = FoC loose + every MEG; custom = `Data\CustomMaps` + each mod's `Data\Art\Maps` |
| Text, fonts, art, audio, video | DAT text, fonts, models, textures, animations, particles, SFX / music / speech, Bink movies | Loaded by name when referenced; font search includes mod layers |
| Effects | `Data\Art\Shaders\**.fx`, `.fxo` | See "Effects and shaders in mods" |

Not supported for any mod: multiplayer, the original game's save files, and retail 32-bit Lua bytecode (the x64 VM
rejects it; retail's own `64Patch.meg` ships text sources for that reason). Lua written for 5.1 or later fails to
compile on 5.0.2, as in retail.

### Effects and shaders in mods

**Original** (legacy `docs/re/RENDER.md` §6): each mod folder's `Data\Art\Shaders\` and `\Terrain\` are searched before
the base ones; a name is looked up as `<name>.fx` first, then `<name>.fxo`, so a `.fx` source wins and is compiled at
run time. The first technique (file order, LOD within Shader Detail) that validates is used. A missing effect draws the
built-in green default; a file that fails to load draws the same under its own name.

**Port**: no fixed shader set; every effect is looked up by name through the VFS, mod folders first
(`engine/materials/include/alamo/materials/effect_loader.hpp`, `effect_audit.hpp`). The renderer draws its own SPIR-V
translation (shaderxlat, HLSL, DXC) of the shipped bytecode, shader model 1-3, on background jobs, cached on disk.
Nothing crashes and nothing is skipped silently: every case but the first row is a `materials::ShaderIssue` (kind,
effect, path, `in_mod`, detail).

| Case | What the port does | Issue kind |
|---|---|---|
| Mod `.fxo` replaces or adds an effect, or a `.fx` the prebuild compiled | Loaded and translated like a shipped effect | none |
| `.fx` beside a `.fxo`, the `.fx` not compiled | The `.fxo` loads (the original compiles the `.fx`) | `source_shadowed` |
| `.fx` without a `.fxo`, not compiled | Green default under its name | `source_only` |
| `.fxo` that does not parse or load | Green default under its name | `load_failed` |
| No selectable technique | Shared green default | `no_technique` |
| Best technique has a pass the port has no program for (VS without PS, SM3 mixed with an older stage) | [port] Skipped like a failed validation; the next drawable technique is used | `technique_skipped` |
| No technique the port can draw | Unlit (base texture, the pass's blend); Heat / Shadow submeshes not drawn | `unsupported_pass` |
| Translation or DXC fails for a pass (including an SM3 feature outside the supported list) | Same unlit fallback, logged once | `translate_failed` |

Issues are available up front through `materials::audit_effects()` / `alamo_effect_audit` (JSON `alamo.effect_audit`
v1), at run time through `Renderer::shader_issues()`, and in the load report's `shaders` section. SM3 limits:
`vs_3_0` / `ps_3_0` pairs are translated with semantic I/O by dcl usage, loops, predicates, relative constant
addressing, `texldl` / `texldd`, `vPos`, `vFace`; vertex texture fetch, non-varying output usages and relative
addressing of `v#` / `o#` are rejected with a message.

### Effect sources and the fx prebuild

The port compiles `.fx` sources ahead of time, never at run time (no D3DCompiler in the engine).
`alamo_fx_prebuild <FoC dir> [MODPATH=..]... [--cache DIR] [--fxc EXE] [--force] [--json FILE|-]` finds every `.fx`
the loader would take (same VFS order), reads it and its `#include`s (own folder first, then `Data\Art\Shaders\`; a
port choice), and runs `fxc.exe /T fx_2_0` on a copy in a fresh temporary folder. The result (compiled effect, or the
fxc log) goes to an `FxCache` entry keyed by a hash of the source and its includes (`$ALAMO_FX_CACHE`, else
`%LOCALAPPDATA%\alamo\fx`; never the game install; written to a temporary name and renamed). The loader
(`LoadOptions::use_fx_sources`, on by default) loads a compiled entry in place of the `.fxo`; no entry or a failed
compile keeps the fallback above, and the issue detail gives the fxc error or says the prebuild has not run.

Whenever a mod is mounted the prebuild runs by itself (`game::prebuild_mod_effects`): `alamo_game` before the renderer
loads its first effect, `alamo_mod_check` before the shader audit (`--no-fx-prebuild` skips it). fxc is `$ALAMO_FXC`,
else `fxc\fxc.exe` beside the exe, else the toolchain's. `fxc.exe` and `d3dcompiler_47.dll` may ship in private test
builds; a public release needs that asked again. Without fxc, cached results still load and other sources report
"fxc.exe not found".

## Fallbacks and report levels

The original loads with most data problems and only logs them; the port does the same and reports them. Nothing is
guessed from a mod's name. **Error**: the data cannot be played; `alamo_game` refuses the session and `alamo_mod_check`
exits 1. **Warning**: the original loads and runs on; so does the port, and the report lists it. **Info**: listed, not
counted.

| Situation | Port | Level |
|---|---|---|
| FoC folder missing | VFS create fails, message names the folder | Error |
| EaW install not found | EaW MEGs and `GameData` skipped | Warning |
| Mod folder without `Data\`; `STEAMMOD=` not installed; `MegaFiles.xml` names a missing or unreadable MEG | Nothing mounted from it | Warning |
| A list file or an object type's model tag names a file no layer has | Skipped; listed under missing files | Warning |
| A loader step throws | Step reported failed with its name | Error |
| A campaign has no planets | Listed as a menu entry that cannot start (retail lists it) | Warning; Error if no campaign has planets |
| Campaign row names an unknown planet / route / faction | Row skipped, diagnostic | Warning |
| `Basic_AI` / `AI_Player_Control` names an undefined AI type | No AI for that player | Warning |
| No skirmish default forces | [port] cheapest-units fallback | Warning |
| Lua does not compile (also a UTF-8 BOM, which retail does not skip) | Listed with the compiler message | Warning |
| A main chunk fails on a `require` no layer has | Listed with the missing name and what loads the script (`named_by`); a mod script nothing names is an unused-script lead | Warning; lead Info |
| A main chunk fails after reading a `GlobalValue` another script sets | Listed as session-dependent with the key and setter | Info |
| XML not well-formed | Lenient reader (digit-led element names parse); recoveries listed as diagnostics | Warning |
| A tag neither retail's table nor the port's loader reads | Listed per database with counts | Warning |
| A submod checked without its parent | Missing files, plus a submod hint naming the installed parent | Info |
| Effect issues | See "Effects and shaders in mods" | Warning (none when compiled) |

`alamo_mod_check [--game DIR] [MODPATH=..] [STEAMMOD=..] [FALLBACKPATH=..] [--out FILE] [--json FILE|-] [--verbose]`
loads the data exactly as `alamo_game` does (`engine/apps/game/src/mod_check.cpp`; exit 0 without errors, 1 with, 2
usage). Each check can be turned off (`--no-campaigns`, `--no-lua`, `--no-lua-run`, `--no-files`, `--no-xml-tags`,
`--no-submod`, `--no-shaders`, `--no-dxc`, `--no-assets`, `--no-fx-prebuild`); `--strict` counts every asset salvage
as a rejection; `--corpus-cache DIR` caches asset verdicts by content hash. Findings from a mod's files are marked
`[mod]` (`in_mod` in JSON), since base FoC itself has some (missing cinematic models, tags the original does not read
either).

### Load report (alamo.mod_report)

`--json` writes one UTF-8 object (non-UTF-8 data bytes become the Latin-1 character of that value), keys in a fixed
order, every list complete (`format_mod_report_json`, `engine/apps/game/src/mod_report_json.cpp`). The current version
is **5**. **Versioning rule:** a new key may be added within a version; a key is never removed or changed in meaning
without raising `version`. Paths are game-relative and upper case unless noted.

| Key | Content |
|---|---|
| `schema`, `version` | `"alamo.mod_report"`, `5` |
| `result`, `errors`, `warnings` | `"ok"` / `"errors"` and the counts (levels above) |
| `game`, `args` | FoC folder; the VFS command line as given |
| `loaded`, `load_error` | Whether the data loaded; why not |
| `mods` | Mounted descriptors: `key`, `name`, `version`, `root`, `game_tag`, `content_hash` |
| `base_eaw`, `base_eaw_suspect` | Refused as a base-EaW mod; the heuristic's reason (warning only) |
| `stack` | `resolved`, `args`, `launch_args`, `mods` [{`key`, `arg`, `name`, `reason`}], `errors`, `warnings`, `fingerprint` |
| `files` | `mod_dirs`, `added`, `replaced`, `folders` [{`folder`, `added`, `replaced`}] |
| `loaders` | Per step in load order: `name`, `ok`, `ms` (wall clock), `diagnostics` |
| `content` | Counts (campaigns, planets, routes, types, factions, AI plans, maps), `ai`, `skirmish_maps` [{`map`, `kind`, `players`, `in_mod`, `custom`}] in menu order |
| `campaigns` | `checked`, `without_planets`, `startable`, `skipped_rows`, `warnings` |
| `missing_files` | `path`, `referenced_by`, `references`, `in_mod` |
| `unknown_xml_tags` | `database`, `tag`, `occurrences`, `types`, `first_file`, `in_mod` |
| `lua` | `scripts`, `compile_errors`, `chunks_run`, `run_errors` (with `missing_require`, `named_by`), `session_errors` (with `global_value`, `set_by`; info), `unused_scripts` (info), `unbound_calls` |
| `shaders` | Audit counts, `fx_sources`, `fx_sources_compiled`, `fx_prebuild` {`ran`, `fxc`, `sources`}, `models_read`, `issues` [{`kind`, `effect`, `path`, `in_mod`, `detail`, `drawn_by`, `models`, `first_model`}] |
| `submod` | `likely`, `missing_list_entries`, `parents` [{`id`, `kind`, `path`, `arg`, `provides`, `mentioned`}], `stacked_args` |
| `check_errors` | Checks that threw (errors) |
| `honour_gaps` | Tags of retail's tables the data sets to a non-default value that the port does not act on: `class`, `tag`, `state`, `uses`, `entries`, `first_file`, `in_mod`, `tier`, `base_uses`, `spec` |
| `retail_equivalent` | `count`, `items` [{`kind`, `subject`, `uses`, `first_file`, `in_mod`, `detail`}]: findings retail shares |
| `bindings` | `called`, `unconsumed` [{`name`, `effect`, `detail`, `call_sites`, `scripts`, `first_script`, `in_mod`}] |
| `story` | `files`, `rewards_used`, `rewards_missing`, `events_used`, `events_partial` |
| `ai` | `token_coverage` {`checked`, `equations`, `leaves`, `missing` [{`token`, `equations`, `first_equation`, `first_file`, `in_mod`}]} |
| `limits` | `limit`, `value`, `needed`: data that hits a guard or a cited retail limit |
| `assets` | Only with a mod mounted and data loaded: `checked`, `decoded`, `salvaged_count`, `rejected_count`, `retail_equivalent`, `salvaged`, `rejected`, `by_family` |
| `grade` | `grade` (`"G0"` / `"G1"`), `g3_static`, `blocking`, `check_errors` |
| `issues` | Every `alamo::CompatIssue` in first-add order: `kind`, `severity`, `subject`, `source`, `layer` (mod index, `"base"` or `"none"`), `retail_equivalent`, `count`, `detail` |

The keys from `honour_gaps` on never count as errors or warnings. `issues` also holds the VFS's findings and one
`loader_diagnostic` record per distinct loader diagnostic line (its layer is that of the first game file the line
names). Those two kinds are added after the grade is set and change no `grade`, `blocking`, `result`, `errors` or
`warnings`: they stay informational. The synthetic mods of `engine/apps/game/testdata/mod_fixture` and
`testdata/conformance/` are checked through this report in ctest.

### Every JSON document the tools write

One writer (`mod_report_json.cpp`: two-space indent, fixed key order, every list complete) makes each document; each
starts with `schema` and `version`, and the versioning rule above applies to all.

| Schema | Version | Written by | Read by |
|---|---|---|---|
| `alamo.mod_report` | 5 | `alamo_mod_check [--resolve] --json` | corpus runner, Command Center (`stack.rs`) |
| `alamo.mod_list` | 1 | `alamo_mod_check --list-mods` | corpus runner (discovery) |
| `alamo.mod_catalog` | 1 | `alamo_mod_check --list [--mod-dir DIR] [--no-hash]` | Command Center (`catalog.rs`) |
| `alamo.mod_conflicts` | 1 | `alamo_mod_check --conflicts <mod args>`: the winner of each file two mods share, by `Vfs::resolve`; findings `list_ignored`, `submod_order`, `no_effect`, `no_data` | Command Center (`conflicts.rs`) |
| `alamo.game_flags` | 1 | `alamo_game --list-flags --json` (`print_flag_list` over `kGameFlags`, then the benchmark parsers' `benchmark_flags()` and `bench_new_flags()`): `flags` [{`flag`, `value`, `one_of`, `positive`}] | Command Center (`launch.rs`) |
| `alamo.effect_audit` | 1 | `alamo_effect_audit --json` | tools |

### The mod picker

`alamo_game`'s main menu has a **Mods** button (dialog `IDD_PORT_MODS_DIALOG`; host `engine/apps/game/app/app_mods.cpp`,
data side `alamo/game/mod_picker.hpp`), shown in live play only. Rows are "Base game (no mod)" and the catalog's rows;
the label is the `modinfo.json` name (else the folder name) and the running mod's row says "(running)". Selecting a row
resolves its stack and runs `check_mod_data` on a worker thread, one at a time, without the runtime DXC compile; the
pane shows the stack, its errors and warnings, the grade badge, the base-EaW refusal or guess, load counts, missing
files, Lua and shader findings, the submod hint, a line per mod with no text in the game's language
(`ui::retail_language_for_langid`), and the launch arguments. **Launch** restarts the executable with its mod arguments
replaced by the row's launch list (a likely submod stacked on its parent); a refused base-EaW mod has no Launch.
The check stays in this process rather than an `alamo_mod_check --json` child process: docs/DECISIONS.md "Mod picker
load report stays in-process".

## Port rules that apply only when a mod layer is loaded

Each is keyed on "a mod layer is loaded" (`vfs::has_mod_layer`), never on which mod. With no mod loaded the port
behaves as base FoC. Add a row in the same change that adds such a rule.

| Rule | Where | Why |
|---|---|---|
| The skirmish buy list also requires a type to be named in some type's `Tactical_Buildable_Objects_Multiplayer` (`SkirmishBuyOptions::multiplayer_list_only`) | `engine/apps/game/rules/skirmish_rules.cpp`, set in `app_screens.cpp` | Mods list option, marker and layer types as buildable; this interim rule drops them |

## Compatibility target

The goal is near-full compatibility with Forces of Corruption mods: a mod that uses the original's data mechanisms
(XML, Lua 5.0 scripts, MEG and asset formats, effects, maps, text, video) loads, looks, sounds and plays in the port as
in retail. Where it does not, the port says why and names the mechanism, and the gap is fixed once as a general
mechanism. Compatibility is measured, not assumed: a grade per mod and stack over the installed corpus, and a coverage
ledger per mechanism.

### The plays checklist

A mod plays when all of these hold:

1. **Load.** `alamo_mod_check` reports no errors; every warning is retail-equivalent or a filed gap.
2. **Front end.** `alamo_game` reaches the mod's main menu, skirmish staging and campaign selection; the hidden smokes
   (`alamo_game --smoke skirmish|campaign --hidden --audio null <mod args>`) reach a running skirmish and campaign.
3. **Skirmish.** A space and a land skirmish on the mod's maps run 3000 ticks AI against AI in `alamo_headless` with AI
   orders and decisions and no port-caused Lua error.
4. **Galactic conquest.** Every campaign the menus offer starts (menu-only entries excepted); one runs 9000 ticks with
   `--story --scoring --object-scripts` with AI decisions and orders and no port-caused Lua error.
5. **Story.** A story campaign reaches its first story events.
6. **Stable and deterministic.** No crash or hang; the same mod and command line give the same per-tick CRC on every
   worker count and compiler; the base-FoC goldens do not move.

Speed is not in the checklist while tier-3 work is paused; a mod too slow to play is filed.

### What may still fail

- What retail also rejects or fails on: copied 32-bit Lua bytecode, a script with a UTF-8 BOM, an error in the mod's
  own script, a listed file the mod does not ship.
- What is out of scope for every mod: multiplayer, the original game's save files, matching retail's crashes or hangs.
- Mods that patch the executable or load native code instead of data.
- Mods made for base Empire at War only (refused, see "Base-EaW mods").

Anything else a mod needs is a port gap.

### Compatibility grades

One grade per mod and per stack; each includes the ones below it. The rules live once in `tools/modcorpus/grade.py`.

| Grade | Name | Meaning |
|---|---|---|
| G0 | Refused | The load report has errors (data does not load, base-EaW mod, stack cycle, check error) or the check failed. Only allowed reasons: out of scope, or data retail cannot play either |
| G1 | Loads | 0 errors |
| G2 | Runs | Space and land skirmish (3000 ticks, workers 0 and 4, CRC logs equal) and one galactic campaign (9000 ticks): exit 0, no timeout, AI orders and decisions, no Lua error beyond documented retail ones. A mod with no map of a kind has that run "n/a" |
| G3 | Plays | G2, the hidden smokes pass, and the report's `grade.g3_static` holds: no honour gap, binding gap, missing reward or token, asset reject or unconfirmed salvage in the mod's own content |
| G4 | Matches | G3, and retail differential checks agree (tier-1 rules exact, tier-2 metrics within the distribution). Needs retail runs; never given by the tools |

What lowers a grade: an error gives G0; an honour gap, `asset_rejected`, `binding_unconsumed`, `reward_missing` or
`token_missing` in content the mod uses keeps it below G3; a retail-equivalent finding never lowers it. The load report
supplies G0 / G1 and the static G3 part; the corpus runner adds the runs (G2) and smokes (G3). A sweep that skipped runs
gives at most G1, one that skipped smokes at most G2. `tools/modcorpus/corpus.json` may list per-setup accepted
blockers (a regex with a `why` and a reference) that are retail-equivalent or by the mod's design; they are reported
under `accepted` and do not lower the grade.

### Reference set

The mods that must reach G4: Thrawn's Revenge (1125571106: largest, heavy Lua framework), Fall of the Republic
(1976399102: same framework, other content), EAW-Remake (1770851727: total conversion replacing base MEGs, own
effects), and Star Wars: Age of Legends (2858787558: mostly loose XML, own root tags, planet variants). A submod is
checked with its parent; a second copy of a mod is not a separate entry. The fixture mod keeps the agnostic route
covered in ctest without game data; it does not replace the set.

## The compatibility system

Not a new module but a set of contracts and generated ledgers across the existing ones, plus three parts: the mod
catalog (`engine/vfs`), the issue record (`engine/core/include/alamo/core/compat_issue.hpp`) and the corpus runner
(`tools/modcorpus/`). Every finding flows through one record type into the load report, the picker and the corpus.

### Principles of the compatibility system

These add to the agnostic-loading rule:

- **Retail is the oracle.** Every behaviour a mod can reach is settled by a `docs/re` spec or a probe kit, never by
  guessing. A port choice where retail is unknown is marked `[port]`.
- **Nothing silent.** Every case where the port does less than retail is a recorded issue with a kind, a source file,
  a layer and a detail. An issue that does not reach the load report is a bug.
- **Coverage is a number, kept by tools.** Each mechanism has a ledger generated from the code and `docs/re`, never
  typed by hand, and a lint keeps it true.
- **Tolerant at run time, strict in tools.** Run-time parsers salvage what retail would survive and record an issue;
  `formats_validate` and `alamo_mod_check --strict` report every inconsistency.
- **No port limit below retail** (see "Limits policy").
- **Do not copy retail's failures, but report them.** Where retail crashes or hangs on a mod's data, the port may stop
  the script or skip the object, reported, with the outcome for a correct mod unchanged. Where retail rejects data
  (BOM scripts, 32-bit bytecode, an undefined function), the port rejects it the same way, so a mod tested in the port
  does not break in retail.
- **Determinism holds for mod data**: every salvage and fallback is a pure function of the bytes and the command line.

### Tag ledger and value semantics

**Values.** XML readers convert by retail's type codes (`engine/data/include/alamo/data/xml_values.hpp`;
`docs/re/xml_field_tables.md` "Type codes"): booleans are true for `1` or a first character `Y` / `T` (any case);
integers and floats are C prefix parses with trailing junk ignored; enums match case-insensitively (unknown: field
unchanged); masks and lists split on space, comma, tab, CR / LF (masks also `|`), unknown type names dropped later;
`TBD` and empty are skipped; duplicate tags follow retail's rule for the class. Element tags carry no behaviour: planets
are any type with the `PLANET` behaviour, galactic roles come from behaviours and categories. An XML file with more
than one root element is read or reported (`xml_extra_root`), never silently truncated. Tags no retail table lists
(`Autoresolve_Health`, `DPS_is`, ...) are retail-equivalent: the binary does not read them either.

**Ledger.** `engine/objects/generated/tag_ledger.inc`, generated by `engine/objects/tools/gen_tag_ledger.py` (human
view with consumer counts in `build/tag-ledger.md`), has one row per (class, tag) of retail's descriptor tables with its
type code, state (`unread`, `stored`, `consumed`, `verified`, or settled as retail-unused / out of scope), spec, use
counts and tier. Reads go through typed accessors so the generator finds consumers; `// ledger: consumes <Tag>` is only
for indirect reads. `tools/lint/check_tag_ledger.py` checks that the ledger matches the code and fails a tag moving
from `consumed` back to `stored`. The load report's `honour_gaps` lists the `unread` / `stored` tags a mod actually sets
to a non-default value. Tags base FoC uses but the port ignores are base-FoC parity bugs first.

### Out-of-scope tag-ledger rows

A third settled table, `OUT_OF_SCOPE` in `gen_tag_ledger.py` beside `RETAIL_UNUSED`: its rows get state
`out_of_scope` and are not honour gaps (`alamo_mod_check` does not count them). Each row names one reason, and
`check_tag_ledger.py` checks the reason is one of these, that nothing in the engine reads the row, and that it is not
also retail-unused:

- **multiplayer**: online-only and MP-only data. A `_MP` row a skirmish or single-player game reads
  (`Override_Bounty_MP`, `Create_Player_In_Multiplayer_Games`, `Campaign.Is_Multiplayer`, ...) is not out of scope.
- **hardware detection**: `HardwareConfiguration` fields; the port uses its own Video options.
- **graphic detail levels**: `GraphicDetailLevel` presets; the renderer has its own settings.
- **debug keys**: debug hot keys and debug displays.
- **localised splash / credits**: per-language splash screens and `Localized_*` overlays (the credits logos are read
  since #458).

### Limits policy

No fixed size in `engine/` sim code may be smaller than retail's. Every integer `constexpr` / `#define` whose name says
it bounds something carries one tag, checked by `tools/lint/check_limits.py` (ctest `alamo_core_limits_lint`):

| Tag | Meaning |
|---|---|
| `// limit: retail <docs/re section / address>` | A size retail has; the citation is required |
| `// limit: data <where>` | Read from data (GameConstants, the type); the constant is a default |
| `// limit: dynamic <why>` | The container grows with the data |
| `// limit: guard <what>` | A sanity bound far above any real data; records `limit_hit` and refuses, never clamps silently |

Deliberate exceptions (a guard that clamps; #143): `kRocketArcMaxDepth` (12, `tactical/combat/rocket.hpp`) stops the
rocket arc subdivision at 2^12 − 1 midpoints. Refusing would leave the rocket without a flight path; the cap only bounds a
recursion that a degenerate radius / threshold pair would not end (retail's never ends on a NaN dot), and the arc it
gives is deterministic.

Retail has no player cap, so per-player state is sized from the session; `sim::kMaxPlayers` = 256 is a guard (the sim's
player ids are `uint8_t`) and `alamo_mod_check` reports `limit_hit` past it. AI level maxima and fleet distance are read
from GameConstants. Squadrons use retail's `Squadron_Units` lists and prices for base FoC and mods. Conformance mods:
`engine/apps/game/testdata/conformance/limits/`.

### Binding effect ledger

All retail Lua bindings exist; the question is whether each has an effect. `engine/lua/tools/gen_bindings.py` derives
an effect state per binding from the code (`engine/lua/include/alamo/lua/bindings.hpp`):

| State | Meaning |
|---|---|
| `full` | Does what retail does (spec cited) |
| `recorded` | Appends a presentation request; its consumer is named |
| `noop-retail` | A no-op in retail too |
| `noop-gap` | A no-op where retail acts |
| `partial` | Some arguments or paths missing |

A test asserts that every `recorded` binding has a consumer in `alamo_game` (an allowed exception says it is
headless-invisible), and the list of gaps may only shrink. The report's `bindings.unconsumed` lists calls a mod makes
to `recorded`-without-consumer, `noop-gap` or `partial` bindings.

### Story ledger

Story event and reward types have the same kind of table (`engine/apps/sim/include/alamo/sim/story_rewards.hpp`:
`retail_story_rewards()`, `story_event_table()`), including a row for every name of retail's reward enum. A reward
name not in retail's enum is skipped and reported retail-equivalent (retail ignores it too); a retail reward the port
lacks is `reward_missing`; an event type dispatched with a documented gap is listed in `story.events_partial`.

### AI token coverage

Perception token handlers register their paths at start-up (`engine/ai/include/alamo/ai/galactic_ai_tokens.hpp`), so
coverage is what the code evaluates, and a test checks the registry and the handlers agree. The report's
`ai.token_coverage` parses every perception equation the mod's AI data defines and lists the leaves the resolver of
that equation's mode does not evaluate. Retail fails a whole equation on an unknown leaf, so a missing leaf silently
turns an AI decision into nil; this report is the only place it shows (`token_missing`).

### Fault containment and the no-abort rule

- **Issue records.** `alamo::CompatIssue` (kind, severity, `retail_equivalent`, subject, source, layer, count, detail)
  is the one record type for every finding. The sink de-duplicates by (kind, subject, source) and counts repeats; it
  is thread-safe and merges in a fixed order. It is diagnostics only: sim logic never reads it and it never enters the
  CRC.
- **No abort.** Data, asset and script errors never abort the process; Lua panics, state-creation failures and
  wrong-thread asserts stay because they are programming errors. A sim invariant broken by data (an object without a
  required behaviour, a null hardpoint target) skips the object or command and records an issue, identically on every
  worker count and compiler.
- **Runaway scripts.** [port] `LuaVmOptions::script_budget` caps the Lua instructions of one resume of a script thread
  (or one host call), far above any normal script. Retail hangs on a script that never yields; the port stops it and
  reports `script_runaway`. The budget is a pure count, so it is deterministic.
- **Fuzzing.** `tools/fuzz` (`alamo_fuzz_smoke`, a `slow`-labelled ctest entry with a short budget per target) covers
  the formats a mod ships: TED maps, DAT text, MTD atlases, `GUIDialogs.rc`, animation clips, particle data, Lua
  chunks, WAV and Bink headers.

### Asset salvage and GUI fallback

Broken mod art matches retail: where retail still draws a defective file the port salvages and draws it, where retail
shows nothing the port shows nothing, and either way it reports it. Each salvage rule is settled against retail with a
probe kit. Run-time decoders salvage (an ALA block shorter than declared repeats the last pose, a broken collision tree
is dropped, a mesh without its info chunk gets defaults, a chunk overrunning its parent is truncated) and record
`asset_salvaged` with the rule; tools stay strict. Texture formats retail's D3D9 samples (bump formats, BC4 / BC5,
16-bit and float, P8, colour-mapped TGA) are converted on load to values D3D9 gave; a DX10-header DDS, which retail's
d3dx9 cannot read, is refused the same way. Retail plays Bink 1 and Bink 2; the port plays Bink 1 only and skips a
Bink 2 movie with a reason (an honour gap, `engine/video/include/alamo/video/bink_header.hpp`). A DAT text file with one bad record
keeps the entries before it (`text_salvaged`). When a mod's GUI file fails to load, the next layer's copy is read
(`Vfs::read_nth`) and `gui_fallback` is recorded. The report's `assets` section decodes every model, particle,
animation, texture, map, sound and movie a mod layer serves with the runtime decoders; a rejection retail shares never
blocks G3.

### Compatibility corpus and its runner

- **Corpus**: every installed workshop mod and its local `Mods\` submods, with their stacks; no extra subscriptions. A
  mod installed later joins automatically. Mods not installed are listed in `corpus.json` as not installed and are not
  graded. The target is G3 on every installed setup, lower only for retail-equivalent or out-of-scope reasons, and G4
  on the reference set.
- **Conformance mods**: small generated mods, one per mechanism, under `engine/apps/game/testdata/conformance/`
  (`stacks`, `limits`, `values`; the `StackTop` stack covers the three-level submod shape). They run in ctest, so a
  regression shows in every build, not only in a sweep.
- **Runner**: `tools/modcorpus/run_corpus.py` (Python, standard library, `-I`) discovers setups through
  `alamo_mod_check --list-mods`, and per setup (cached while the content hash and build are unchanged) runs the load
  report, the space and land skirmishes on workers 0 and 4 with `--compare`, one galactic campaign, and the hidden
  smokes (`--stages check,runs,smokes`; `--asan` adds sanitizer runs). Results go to `logs/modcorpus/<date>/`. It exits
  non-zero when a grade drops against the baseline and names the first new blocker. `--list` prints the detected set.

### Determinism with mods

The runner's 0 / 4 worker comparison covers worker counts. Each reference mod also has a short headless scenario in
`tests/goldens/mods/<name>.json`, run by `tools/goldens/mod_golden.py` as ctest `alamo_headless_mod_golden_<name>`,
skipped when `ALAMO_FOC_DATA` is unset or the mod is not installed. A spec's `fingerprint` is the mod's content hash
(`vfs::mod_content_hash`, no write times), printed by `mod_golden.py --spec S --fingerprint --mod-check
alamo_mod_check.exe` from `alamo_mod_check --list-mods`; it is informational (#349). Mods set values the stock game
never uses (delays of 99999999999 seconds); sim code converts them with x86 range behaviour (`alamo/core/x86_int.hpp`,
`alamo/core/frames.hpp`; `tools/lint/check_frame_casts.py` enforces it outside `engine/core`), and
`tools/conformance/limit_mods.py` checks the CRC logs agree across worker counts on such data. All other rules are in
`docs/SIMULATION.md`.

### Saves, mod versions and the launcher

A save records its stack (see "What a save remembers"); a refusal names the mod and both versions, and "Load anyway" is
offered when only data differs, with outdated-save sessions unsupported. The picker and the Command Center show the
resolved stack (a submod pulls in its declared or likely parent), the grade and blockers, the mod's supported languages
against the game's, and conflict warnings (two total conversions, a submod above its parent).

### Standing mod decisions

| Decision | Rule |
|---|---|
| FoC mods only | Base-EaW mods are detected and refused with the reason (G0); the heuristic alone only warns |
| Retail squadrons | Squadrons use retail's `Squadron_Units` lists and prices for base FoC and mods; no resize or price scaling |
| Load anyway | A save from another mod version may load when its saved state still reads against the current data: in practice the type list is unchanged and every referenced faction and script exists. A save whose type list gained or reordered types is refused (a remap by name is #144, out of focus). The session is marked; continuing it is not supported (narrowed 2026-11-22 to what is built) |
| Element tags carry no behaviour | Everywhere; base-FoC golden moves are approved once the change is shown to match retail |
| Broken art matches retail | Salvage where retail draws, nothing where retail shows nothing, always reported; `--strict` lists every defect |
| Corpus and target | The installed mods and their stacks; G3 on every installed setup, G4 on the reference set |
| One mod environment | `vfs::ModEnvironment` owns discovery, descriptors, stack resolution, mount plan and provenance; picker, `alamo_play` and the Command Center keep no copies; mods and the retail load order do not change |
| One launch list | Every surface starts `ModStack::launch_args`, also for a stack with errors (base-EaW excepted) |
| Junction-joined folders | Keep two catalog rows and two argument forms |
| VFS findings and loader diagnostics | Informational: they never change a grade, `result`, `errors` or `warnings` |

## Checklist run

The checklist above, for every installed mod and stack, is automated by the corpus runner. The block between the
markers is generated; do not edit it by hand. Per-setup results, command output and load reports stay in
`logs/modcorpus/<date>/`.

<!-- modcorpus:begin -->
Filled by `tools/modcorpus/run_corpus.py --write-md --md docs/MODS.md`.
<!-- modcorpus:end -->
