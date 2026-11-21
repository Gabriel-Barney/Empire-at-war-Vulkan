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
| `.\dev.cmd test -LE slow` | Run the fast CTest suite after a build |
| `.\dev.cmd test -R alamo_core_tests` | Run tests matching a regular expression |
| `.\dev.cmd test` | Run all configured CTest tests |
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

Some tests skip when game data or a Vulkan device is unavailable. The optional `extracted\foc_data_text.zip` enables XML conformance checks during configuration. Retail trace comparisons read captures from `ALAMO_Q2_TRACES`. Keep game files, captures and machine settings local.

## Working with Claude Code or Codex

Open this directory as the project root in either tool. Both use the development guide in `AGENTS.md`.
