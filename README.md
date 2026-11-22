# Empire at War Vulkan

A Windows x64 port of the Empire at War engine. The C++20 engine uses MSVC and Vulkan. The Rust Command Center launcher uses the Windows MSVC target.

## Build and test

Open PowerShell in the project root. This checkout has a local toolchain. On a fresh checkout, install 64-bit Python 3.10 or newer and run `.\toolchain\2_setup.cmd`. Initial setup downloads the compiler, Windows SDK, CMake, Ninja, Rust and engine dependencies. The installer prompts for the Microsoft license when installing MSVC.

`dev.cmd` is the one development command; `.\dev.cmd help` lists everything. It is a thin shim over `tools\dev.py`, which passes your arguments to CMake, CTest and the suite runner unchanged. `cmd` reads `|`, `&`, `<` and `>` in an argument before `dev.py` sees it, so keep double quotes around such an argument all the way to `cmd`. In PowerShell, wrap the double-quoted argument in single quotes: `.\dev.cmd test -R '"alamo_skills|alamo_dev_selftest"'`. In `cmd`, plain double quotes are enough. Build trees come from `CMakePresets.json`: `fast` is `build\` (the default) and `full` is `build-full\`.

| Command | Result |
| --- | --- |
| `.\dev.cmd doctor` | Report the toolchain, SDK, dependencies and game-data settings, with the fix for anything missing |
| `.\dev.cmd check` | Build with warnings as errors and run the synthetic suite: the check every change needs, and what CI runs |
| `.\dev.cmd check --quick` | The same without tests labelled `slow` (the inner loop) |
| `.\dev.cmd check --target alamo_core_tests` | Build one target and run only the tests whose names start with it |
| `.\dev.cmd check --preset full` | Fresh configure and clean build in `build-full\`, then every suite |
| `.\toolchain\2_setup.cmd --check` | Verify installed dependencies offline |
| `.\dev.cmd configure` | Generate the Release build in `build\` |
| `.\dev.cmd build` | Compile the engine, tools, and C++ tests |
| `.\dev.cmd build alamo_game` | Compile the game and its dependencies |
| `.\dev.cmd suite synthetic` | Run the required synthetic suite and print the per-suite report |
| `.\dev.cmd suite synthetic --fast` | The same without tests labelled `slow` (the inner loop) |
| `.\dev.cmd suite all` | Run every suite; retail and Vulkan entries skip with a reason when the machine lacks them |
| `.\dev.cmd suite retail` | Run the retail-data suite; missing game data fails the run |
| `.\dev.cmd test -R alamo_core_tests` | Run CTest directly with any CTest options |
| `.\dev.cmd launcher` | Build `launcher\target\release\command-center.exe` |
| `.\dev.cmd launcher-test` | Run the launcher tests |

A failed step prints the first meaningful diagnostic, the toolchain and commit, a command that reproduces it, the elapsed time and the log path. Every run's log is in `build\logs\`, and `build\logs\dev-last.json` holds the last result.

Project code compiles with `/WX`. Configure with `-DALAMO_WARNINGS_AS_ERRORS=OFF` to see a warning backlog without stopping.

## Continuous integration

`.github/workflows/landing.yml` runs on every pull request into `main` and reports one required status, `landing`. `tools\landing_gate.py` classifies the changed paths and turns the job results into that status: a skipped, cancelled, failed or missing result of the job a change needs fails it, naming the job. The `landing` job's summary records the pull request's head, its base and the tested merge candidate. A pull request that changes simulation code (`tests\simulation_paths.txt`) needs a `Behaviour:` line in its description, and `landing` fails without one (`docs\SIMULATION.md` "Declared behaviour changes"). A change to prose only (top-level and `docs\*.md`, `docs\agents\`) runs the doc-reference lint. Any other change installs the pinned toolchain without Rust (`2_setup.cmd --engine-only --accept-msvc-license`, cached by the setup scripts' hash), then runs `dev.cmd doctor` and `dev.cmd check` with compiles going through sccache. The compiler cache is saved once per commit and restored from the pull request's own newest entry, else `main`'s, among entries for the same sccache version and toolchain setup scripts. A pull request with no such entry builds with the shared PCH instead (about 22 minutes against 37 for a cold sccache build) and saves no compiler cache, so every push of a pull request that changes the toolchain builds that way until it lands. A push to `main` is never cancelled by a later one, because its run seeds the caches every new pull request starts from. The test results and `build\logs\` are attached to the run. `.github/workflows/nightly.yml` runs the MSVC AddressSanitizer check (`dev.cmd check --preset asan`, build folder `build-asan`: the synthetic suite under ASan) and the MSVC `/analyze` check (`dev.cmd check --preset analyze`, build folder `build-analyze\`) every night and on demand. A finding missing from `tests\analyze_baseline.txt` fails it, and the issue "Nightly checks: last passing" records each check's last passing date and commit. Retail, Vulkan and trace checks stay local (`docs\DECISIONS.md` "Retail and GPU evidence is a recorded local run").

`main` is protected by the repository ruleset "main: landing required". Every change lands through a pull request as one squashed commit, so `main` has a linear history. `landing`, reported by GitHub Actions, must pass on the pull request. The branch does not have to be up to date with `main`: the pull request run tests the branch merged with `main` as it was then, and the run on every push to `main` catches a break between two changes that landed close together (`docs\DECISIONS.md` "Pull requests land squashed without an up-to-date rule"). Force pushes and deleting `main` are blocked, and no one bypasses the ruleset. No approving review is required, because the maintainer cannot approve their own pull request. Work on a branch from the latest `main`, push it, and open a pull request into `main`. `gh pr merge --auto --squash` lands it when `landing` passes, and the branch is deleted after the merge.

In Command Center, select `build\bin\alamo_game.exe` as the engine executable and choose the installed game folder.

## Game data

Set `ALAMO_FOC_DATA` to the Forces of Corruption installation directory and `ALAMO_EAW_DATA` to the base game's installation directory. Each path should contain a `Data\` folder. For persistent settings on this machine, use `dev.local.cmd` at the project root. Example contents follow.

```bat
@echo off
set "ALAMO_FOC_DATA=D:\Games\Star Wars Empire at War\corruption"
set "ALAMO_EAW_DATA=D:\Games\Star Wars Empire at War\GameData"
```

The optional `extracted\foc_data_text.zip` enables XML conformance checks during configuration. Retail trace comparisons read captures from `ALAMO_Q2_TRACES`. Keep game files, captures and machine settings local.

The optional `extracted\format_examples\` folder holds glTF exports and reference renders of game models for `alamo_assets_tests` and `alamo_render_reference_tests`. Without it, those glTF checks skip. They are made from the installed game. For example, extract `DATA\ART\MODELS\EI_ROYALGAURD.ALO` and `EI_ROYALGAURD_IDLE_00.ALA` from FoC's `Data\models.meg` with `build\bin\megtool.exe extract`, then run `build\bin\alo2gltf.exe --anim EI_ROYALGAURD_IDLE_00.ALA EI_ROYALGAURD.ALO extracted\format_examples\EI_ROYALGAURD.gltf`. The reference PNGs come from `tools\formats\python\render_gltf.py`. Game assets and anything made from them are never committed.

## Test suites

Every CTest entry belongs to one suite, carried as a CTest label:

| Suite | Needs | Notes |
| --- | --- | --- |
| `synthetic` | Nothing outside the checkout | Runs with the game-data variables cleared and Vulkan drivers hidden (`VK_LOADER_DRIVERS_DISABLE=*`), so it behaves the same on every machine. Hosted CI runs this suite and it must pass. |
| `retail` | `ALAMO_FOC_DATA` and sometimes `ALAMO_EAW_DATA` | Also covers entries that need `extracted\foc_data_text.zip`. |
| `vulkan` | A Vulkan 1.3 device | |
| `traces` | `ALAMO_Q2_TRACES` | |

A test that mixes kinds of checks has a synthetic entry plus companion entries named `<test>_retail`, `<test>_vulkan` or `<test>_retail_vulkan`. A skipped data or GPU part therefore appears as its own skipped result instead of hiding inside a pass. `tests\test_suites.txt` lists the tests that are not purely synthetic. Update it when a test gains a data or GPU part.

The `retail`, `vulkan` and `traces` entries run behind `alamo_test_gate`. It checks the prerequisites first. If any are missing, it exits with skip code 77 and prints the reason. A `SKIP ...` line from the test inside a gated entry also turns the entry into a skip. When a suite is required, missing prerequisites fail the run instead. `dev.cmd suite retail` requires `retail`. Add `--require retail,vulkan` to `dev.cmd suite all` to require more suites, or set `ALAMO_TEST_REQUIRE`.

`dev.cmd suite` writes `build\test-results\report-<suite>.md`. The report counts passed, failed, skipped and flaky entries by suite and gives the reasons for skips. A synthetic run fails when an entry listed in `tests\required_synthetic.txt` is missing from the build tree, for example because a module was left out or Python was not found. After adding or renaming a synthetic test, regenerate the list with `dev.cmd suite synthetic --write-required` and review the diff.

Each failed test gets one retry, except determinism checks. A test that passes on the retry counts as passed and appears as flaky in the report. Its first failing output is kept in `build\test-results\flaky.jsonl`.

Tests write images and reports to `build\artifacts\<entry>\`, never to the source tree.

## Working with Claude Code or Codex

Open this directory as the project root in either tool. Both use the development guide in `AGENTS.md`.
