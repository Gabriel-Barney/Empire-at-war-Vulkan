// Install locations (Stage 10 unified mod system): where this machine keeps Forces of Corruption, the EaW
// folder next to it, the Steam client and library folders and the folders the user named, found once and handed to
// every launch surface (alamo_game's mod picker, alamo_play, alamo_mod_check). Before ums1 each surface walked Steam on
// its own (find_steam_libraries call sites, play::find_game's library loop).
//
// The game search order is Play.cmd's (launcher.ps1 Find-Game): the user's folders in the given order (game_path.txt,
// then ALAMO_FOC_DATA), then <library>\steamapps\common\Star Wars Empire at War\corruption of each Steam library, then
// the fixed install folders (GOG, disc). A folder counts when it is a FoC folder (Data, and Data\MegaFiles.xml or
// StarWarsG.exe) or the "Star Wars Empire at War" folder above one. The Steam libraries of a found game are
// find_steam_libraries(foc, steam_roots): the library holding the game first, so its own workshop items are found even
// with no Steam client known.
//
// Read only: nothing here writes a file, the registry or calls Steam. The result depends only on the inputs and the
// files on disk (no enumeration order decides it). Launcher-side: nothing in the sim reads it. #342 adds the inputs a
// host fills when it has no folder (the registry's install entries, the shared fixed folders) and a bounded drive scan
// for the last resort; game::auto_find_game (alamo_game_open) puts them together and remembers the result.
#pragma once
#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace alamo::vfs {

// A Forces of Corruption folder: has Data, and Data\MegaFiles.xml or StarWarsG.exe (launcher.ps1 Test-Game).
bool is_foc_dir(const std::string& dir);
// `text` as a game folder (UTF-8): the "corruption" folder itself, or the "Star Wars Empire at War" folder above it.
// Spaces, tabs, line ends and quotes around it and trailing separators are dropped. "" when it is neither.
std::string normalize_foc_dir(std::string_view text);

struct InstallInputs {
    std::vector<std::string> user_folders;     // folders the user named, in order (game_path.txt line, ALAMO_FOC_DATA)
    std::vector<std::string> steam_roots;      // Steam client folders (default_steam_roots())
    std::vector<std::string> steam_libraries;  // library folders searched after the roots' libraries (tests, odd setups)
    std::vector<std::string> fixed;            // last-resort install folders (GOG, disc)
};

struct InstallLocations {
    std::string foc;  // the FoC "corruption" folder; "" when none was found
    std::string eaw;  // <foc>\..\GameData when it has a Data folder (the EaW install retail's FoC reads), else ""
    std::vector<std::string> steam_roots;      // as given (or default_steam_roots())
    std::vector<std::string> steam_libraries;  // find_steam_libraries(foc, steam_roots), then the extra libraries
    std::vector<std::string> user_folders;     // as given
    // The user's extra mod folders (the Command Center's "extra folders"): each one mod (it has a Data folder) or a
    // folder of mods. Not searched for a game; scan_mod_catalog lists them after <foc>\Mods. Set by the caller.
    std::vector<std::string> mod_folders;
    // <foc>\Mods ("" without a game folder).
    std::string mods_dir() const;
};

// The search above. With no game found, `foc` and `eaw` are "" and the libraries are those of the roots alone.
InstallLocations find_install(const InstallInputs& in);
// The locations of a game folder the caller already has (alamo_game --game, alamo_mod_check --game): no search.
InstallLocations install_at(std::string_view foc, std::span<const std::string> steam_roots);
// install_at(foc, default_steam_roots()).
InstallLocations default_install_at(std::string_view foc);

// ---- #342: finding the game with no folder given ------------------------------------------------------------------
// The registry's install entries (read only), for InstallInputs::fixed: GOG Galaxy's SOFTWARE\GOG.com\Games\<id> "path"
// values and the retail disc's SOFTWARE\LucasArts\Star Wars Empire at War[ Forces of Corruption]\1.0 "InstallPath", in the
// 64- and 32-bit views (HKLM, then HKCU). Folders that exist, in that order, each once; find_install checks each with
// normalize_foc_dir (the folder itself or its "corruption" folder).
std::vector<std::string> registry_install_folders();
// The fixed last-resort install folders every launch surface shares (GOG's defaults; Play.exe's list before #342).
std::vector<std::string> default_fixed_install_folders();
// The roots of the fixed local drives ("C:\", "D:\", ...), in drive-letter order: GetDriveTypeW == DRIVE_FIXED only
// (removable, network, CD and RAM drives are left out).
std::vector<std::string> fixed_drive_roots();

// The last-resort folder scan: breadth-first over every root together (a shallow install on D: is found before a deep
// folder on C:), each folder's subfolders in a fixed order - the likely names first (Star Wars Empire at War,
// corruption, SteamLibrary, steamapps, common, GOG Games, Games, Program Files, Program Files (x86)), then by name
// ignoring case - so the result depends only on the folders on disk. A folder matches when it, or its "corruption"
// folder, is a FoC folder (is_foc_dir); the first match ends the scan. Reparse points (junctions, symbolic links) and the
// system and tool folders (Windows, Windows.old, $Recycle.Bin, System Volume Information, Recovery, $WinREAgent,
// ProgramData, AppData, node_modules, .git) are not entered. Folders it cannot list are skipped.
struct FocScan {
    std::vector<std::string> roots;  // UTF-8 folders (fixed_drive_roots() for the real machine)
    uint32_t max_depth = 6;          // levels below a root; C:\Program Files (x86)\Steam\steamapps\common\<game>\corruption is 6
    uint64_t max_folders = 200000;   // folders checked in all, roots included
    uint64_t progress_every = 5000;  // progress(...) after every this many folders (0: never)
    std::function<void(uint64_t checked, const std::string& at)> progress;
};
struct FocScanResult {
    std::string foc;        // the FoC "corruption" folder found, "" when none
    uint64_t checked = 0;   // folders checked
    bool limit_hit = false; // stopped at max_folders
};
FocScanResult scan_for_foc(const FocScan& scan);

}  // namespace alamo::vfs
