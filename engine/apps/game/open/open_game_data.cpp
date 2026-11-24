#include "alamo/game/open_game_data.hpp"

#include <cstdlib>
#include <exception>
#include <filesystem>
#include <string>
#include <utility>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include "alamo/core/bytes.hpp"
#include "alamo/core/host_fs.hpp"
#include "alamo/vfs/host_path.hpp"
#include "alamo/vfs/install_locations.hpp"
#include "alamo/vfs/steam_workshop.hpp"

namespace alamo::game {

namespace {

// #137: the wide value as UTF-8 (the vfs's path strings), so a game folder outside the ANSI code page keeps its name.
std::string env_text(const char* name) {
    std::wstring wname;
    for (const char* c = name; *c; ++c) wname.push_back(static_cast<wchar_t>(static_cast<unsigned char>(*c)));  // ASCII names
    wchar_t* v = nullptr;
    size_t len = 0;
    std::wstring value;
    if (_wdupenv_s(&v, &len, wname.c_str()) == 0 && v) value = v;
    std::free(v);
    if (value.empty()) return {};
    try {
        const std::u8string s = std::filesystem::path(value).u8string();
        return std::string(reinterpret_cast<const char*>(s.data()), s.size());
    } catch (const std::exception&) {  // no UTF-8 form (a lone UTF-16 surrogate)
        return {};
    }
}

}  // namespace

std::string game_data_root(std::string_view game_dir) { return game_dir.empty() ? env_text("ALAMO_FOC_DATA") : std::string(game_dir); }

std::optional<std::string> eaw_data_from_env() {
    std::string eaw = env_text("ALAMO_EAW_DATA");
    if (eaw.empty()) return std::nullopt;
    return eaw;
}

namespace {

// game_path.txt's first line as UTF-8 (a UTF-8 BOM dropped); "" when there is no file.
std::string read_saved_folder(const std::string& file) {
    std::string text;
    if (!read_text_file(file, text)) return {};
    if (text.size() >= 3 && static_cast<unsigned char>(text[0]) == 0xEF && static_cast<unsigned char>(text[1]) == 0xBB &&
        static_cast<unsigned char>(text[2]) == 0xBF)
        text.erase(0, 3);
    const size_t eol = text.find_first_of("\r\n");
    if (eol != std::string::npos) text.resize(eol);
    return text;
}

}  // namespace

AutoFindResult auto_find_game(const AutoFindInputs& in) {
    AutoFindResult r;
    const std::string file = in.exe_dir.empty() ? std::string() : path_to_utf8(path_from_utf8(in.exe_dir) / "game_path.txt");
    const std::string saved = file.empty() ? std::string() : read_saved_folder(file);
    vfs::InstallInputs v;
    if (!saved.empty()) v.user_folders.push_back(saved);
    if (!in.env.empty()) v.user_folders.push_back(in.env);
    v.steam_roots = in.steam_roots;
    v.fixed = in.fixed;
    r.foc = vfs::find_install(v).foc;
    if (r.foc.empty() && !in.scan_roots.empty()) {
        vfs::FocScan scan;
        scan.roots = in.scan_roots;
        scan.max_depth = in.max_depth;
        scan.max_folders = in.max_folders;
        scan.progress = in.progress;
        if (in.progress) in.progress(0, {});
        const vfs::FocScanResult s = vfs::scan_for_foc(scan);
        r.scanned = true;
        r.checked = s.checked;
        r.limit_hit = s.limit_hit;
        r.foc = s.foc;
    }
    // remember it, unless game_path.txt already names this folder
    if (!r.foc.empty() && !file.empty()) {
        const std::string had = vfs::normalize_foc_dir(saved);
        if (had.empty() || vfs::lexical_folder_key(had) != vfs::lexical_folder_key(r.foc))
            r.saved = write_text_file(file, vfs::native_utf8(path_from_utf8(r.foc).make_preferred()) + "\r\n", &r.save_error);
    }
    return r;
}

AutoFindInputs default_auto_find_inputs(std::string exe_dir) {
    AutoFindInputs in;
    if (exe_dir.empty()) {  // this executable's folder
        std::wstring exe(32768, wchar_t{});
        const DWORD n = GetModuleFileNameW(nullptr, exe.data(), static_cast<DWORD>(exe.size()));
        if (n > 0 && n < exe.size()) {
            exe.resize(n);
            exe_dir = vfs::native_utf8(std::filesystem::path(exe).parent_path());
        }
    }
    in.exe_dir = std::move(exe_dir);
    in.env = env_text("ALAMO_FOC_DATA");
    in.steam_roots = vfs::default_steam_roots();
    in.fixed = vfs::registry_install_folders();
    for (std::string& f : vfs::default_fixed_install_folders()) in.fixed.push_back(std::move(f));
    in.scan_roots = vfs::fixed_drive_roots();
    return in;
}

OpenedGameData open_game_data(std::string_view game_dir, std::span<const std::string> args, IssueSink& issues, OpenGameDataOptions options) {
    OpenedGameData out;
    out.root = game_data_root(game_dir);
    if (out.root.empty()) return out;  // NoData
    vfs::ModEnvironmentOptions mopt;
    mopt.eaw_root = std::move(options.eaw_root);
    mopt.hash = options.hash;
    out.env = vfs::ModEnvironment::open(out.root, args, issues, &out.error, std::move(mopt));
    if (!out.env) {
        out.status = OpenedGameData::Status::VfsError;
    } else if (out.env->refused()) {  // D1: FoC mods only
        out.status = OpenedGameData::Status::Refused;
        out.error = out.env->refusal();
    } else {
        out.status = OpenedGameData::Status::Opened;
        out.error.clear();
    }
    return out;
}

}  // namespace alamo::game
