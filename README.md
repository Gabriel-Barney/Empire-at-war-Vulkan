# Empire at War Vulkan

A Windows x64 port of the Empire at War engine. The C++20 engine uses MSVC and Vulkan. The Rust Command Center launcher uses the Windows MSVC target.

## Build and test

Open PowerShell in the project root. This checkout has a local toolchain. On a fresh checkout, install 64-bit Python 3.10 or newer and run `.\toolchain\2_setup.cmd`. Initial setup downloads the compiler, Windows SDK, CMake, Ninja, Rust and engine dependencies. The installer prompts for the Microsoft license when installing MSVC.

| Command | Result |
| --- | --- |
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

In Command Center, select `build\bin\alamo_game.exe` as the engine executable and choose the installed game folder.

## Game data

Set `ALAMO_FOC_DATA` to the Forces of Corruption installation directory and `ALAMO_EAW_DATA` to the base game's installation directory. Each path should contain a `Data\` folder. For persistent settings on this machine, use `dev.local.cmd` at the project root. Example contents follow.

```bat
@echo off
set "ALAMO_FOC_DATA=D:\Games\Star Wars Empire at War\corruption"
set "ALAMO_EAW_DATA=D:\Games\Star Wars Empire at War\GameData"
```

The optional `extracted\foc_data_text.zip` enables XML conformance checks during configuration. Retail trace comparisons read captures from `ALAMO_Q2_TRACES`. Keep game files, captures and machine settings local.

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
