// the one way every host opens the game's data - alamo_game (load_game_data) and each alamo_headless runner
// (--economy, --story, --map, --battles, --galactic-ai, --skirmish-ai foc, the save runs). It picks the FoC folder
// (`game_dir`, else ALAMO_FOC_DATA), opens it through vfs::ModEnvironment (the mod stack from the VFS arguments) and turns
// the D1 base-EaW refusal into a status, so no host can forget that check. What each host does with a failure (exit code,
// skip line, error text) stays the host's.
//
// Library alamo_game_open (vfs + core only): alamo_headless's Phase 2 main.cpp builds without alamo_sim, where
// alamo_game_core does not exist, so the function sits in this small library that alamo_game_core links publicly.
#pragma once
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "alamo/core/compat_issue.hpp"
#include "alamo/vfs/mod_environment.hpp"

namespace alamo::game {

// The FoC folder a host opens: `game_dir` when given, else the ALAMO_FOC_DATA environment variable ("" when neither).
std::string game_data_root(std::string_view game_dir);
// ALAMO_EAW_DATA as an explicit EaW tier (nullopt when unset or empty: the VFS auto-detects <game>\..\GameData).
std::optional<std::string> eaw_data_from_env();

// #342: finding the FoC folder when a host was given none (alamo_game started directly: no --game, no ALAMO_FOC_DATA).
// 1. The lookups, vfs::find_install's order: <exe_dir>\game_path.txt (its first line, UTF-8), `env`, the Steam roots and
//    libraries, then `fixed` (the registry's install entries and the shared fixed folders on a real machine).
// 2. When they all miss: vfs::scan_for_foc over `scan_roots` (the fixed drives), bounded by max_depth / max_folders.
// 3. A folder found by 2 - or by 1 other than a game_path.txt that already names it - is written to game_path.txt (one
//    UTF-8 line, the file Play.exe and launcher.ps1 read), so the scan runs once; a saved folder that is no longer a FoC
//    folder is passed over and the search runs again. Nothing is written when nothing is found.
// The EaW folder needs nothing more: the VFS takes <foc>\..\GameData (OpenGameDataOptions::eaw_root nullopt).
struct AutoFindInputs {
    std::string exe_dir;                   // UTF-8; game_path.txt lives here ("" : no file read or written)
    std::string env;                       // ALAMO_FOC_DATA
    std::vector<std::string> steam_roots;  // vfs::default_steam_roots()
    std::vector<std::string> fixed;        // vfs::registry_install_folders(), then vfs::default_fixed_install_folders()
    std::vector<std::string> scan_roots;   // vfs::fixed_drive_roots() (empty: no scan)
    uint32_t max_depth = 6;
    uint64_t max_folders = 200000;
    std::function<void(uint64_t checked, const std::string& at)> progress;  // the scan's progress (every 5,000 folders)
};
struct AutoFindResult {
    std::string foc;       // the folder found, "" when none
    bool scanned = false;  // the drive scan ran
    bool saved = false;    // game_path.txt was written
    uint64_t checked = 0;  // folders the scan checked
    bool limit_hit = false;
};
AutoFindResult auto_find_game(const AutoFindInputs& in);
// The real machine's inputs: game_path.txt in `exe_dir` (UTF-8; "" = this executable's folder), ALAMO_FOC_DATA, the
// Steam roots, the registry and fixed folders, the fixed drives.
AutoFindInputs default_auto_find_inputs(std::string exe_dir = {});

struct OpenGameDataOptions {
    // vfs::ModEnvironmentOptions::eaw_root (nullopt = auto-detect). The headless galactic runs pass eaw_data_from_env() on
    // the environment route (no --game); everything else auto-detects.
    std::optional<std::string> eaw_root;
    // vfs::ModEnvironmentOptions::hash: each mod's content hash. alamo_game's saves need it; the headless runs read none.
    bool hash = false;
};

struct OpenedGameData {
    enum class Status : uint8_t {
        Opened,    // env is the game's files
        NoData,    // no folder: neither game_dir nor ALAMO_FOC_DATA
        VfsError,  // the folder does not open (error = the VFS reason)
        Refused,   // D1: a base-EaW mod in the stack (error = the refusal; env is kept so a report can name the mod)
    };
    Status status = Status::NoData;
    std::string root;                          // the folder tried ("" for NoData)
    std::unique_ptr<vfs::ModEnvironment> env;  // set for Opened and Refused
    std::string error;
    bool ok() const { return status == Status::Opened; }
};

// Opens game_data_root(game_dir) with the VFS arguments `args` (MODPATH= / STEAMMOD= / FALLBACKPATH=). `issues` receives the
// VFS findings while it opens.
OpenedGameData open_game_data(std::string_view game_dir, std::span<const std::string> args, IssueSink& issues,
                              OpenGameDataOptions options = {});

}  // namespace alamo::game
