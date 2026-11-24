// Install locations (install_locations.hpp).
#include "alamo/vfs/install_locations.hpp"

#include <algorithm>
#include <deque>
#include <filesystem>
#include <iterator>
#include <system_error>
#include <utility>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include "alamo/core/ascii.hpp"
#include "alamo/core/host_fs.hpp"
#include "alamo/vfs/host_path.hpp"
#include "alamo/vfs/steam_workshop.hpp"

namespace alamo::vfs {
namespace fs = std::filesystem;

namespace {

bool dir_exists(const fs::path& p) {
    std::error_code ec;
    return fs::is_directory(long_path(p), ec);
}

bool path_exists(const fs::path& p) {
    std::error_code ec;
    return fs::exists(long_path(p), ec);
}

std::string trim(std::string_view s) {
    const auto space = [](char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '"'; };
    size_t b = 0;
    while (b < s.size() && space(s[b])) ++b;
    size_t e = s.size();
    while (e > b && space(s[e - 1])) --e;
    return std::string(s.substr(b, e - b));
}

// <foc>\..\GameData when it has a Data folder.
std::string eaw_next_to(const std::string& foc) {
    if (foc.empty()) return {};
    const fs::path g = path_from_utf8(foc).parent_path() / "GameData";
    return dir_exists(g / "Data") ? path_to_utf8(g) : std::string();
}

void fill(InstallLocations& out, std::span<const std::string> extra_libraries) {
    out.eaw = eaw_next_to(out.foc);
    out.steam_libraries = find_steam_libraries(out.foc, out.steam_roots);
    for (const std::string& lib : extra_libraries) {
        bool dup = lib.empty();
        for (const std::string& have : out.steam_libraries) dup = dup || lexical_folder_key(have) == lexical_folder_key(lib);
        if (!dup) out.steam_libraries.push_back(lib);
    }
}

}  // namespace

bool is_foc_dir(const std::string& dir) {
    if (dir.empty()) return false;
    const fs::path d = path_from_utf8(dir);
    if (!dir_exists(d / "Data")) return false;
    return path_exists(d / "Data" / "MegaFiles.xml") || path_exists(d / "StarWarsG.exe");
}

std::string normalize_foc_dir(std::string_view text) {
    std::string t = trim(text);
    while (t.size() > 3 && (t.back() == '\\' || t.back() == '/')) t.pop_back();  // keep "C:\"
    if (t.empty()) return {};
    if (is_foc_dir(t)) return t;
    const std::string below = path_to_utf8(path_from_utf8(t) / "corruption");
    if (is_foc_dir(below)) return below;
    return {};
}

std::string InstallLocations::mods_dir() const { return foc.empty() ? std::string() : path_to_utf8(path_from_utf8(foc) / "Mods"); }

InstallLocations find_install(const InstallInputs& in) {
    InstallLocations out;
    out.steam_roots = in.steam_roots;
    out.user_folders = in.user_folders;
    for (const std::string& u : in.user_folders)
        if (out.foc.empty()) out.foc = normalize_foc_dir(u);
    if (out.foc.empty()) {
        // The roots' libraries, then the extra ones: the same list a found game gets, minus its own library.
        std::vector<std::string> libs = find_steam_libraries("", in.steam_roots);
        libs.insert(libs.end(), in.steam_libraries.begin(), in.steam_libraries.end());
        for (const std::string& lib : libs) {
            if (!out.foc.empty()) break;
            // [name-ok: Steam's install folder of the game, not a faction]
            out.foc = normalize_foc_dir(path_to_utf8(path_from_utf8(lib) / "steamapps" / "common" / "Star Wars Empire at War"));
        }
    }
    for (const std::string& f : in.fixed)  // #342: the folder or its "corruption" folder (the registry names the EaW one)
        if (out.foc.empty()) out.foc = normalize_foc_dir(f);
    fill(out, in.steam_libraries);
    return out;
}

InstallLocations install_at(std::string_view foc, std::span<const std::string> steam_roots) {
    InstallLocations out;
    out.foc = std::string(foc);
    out.steam_roots.assign(steam_roots.begin(), steam_roots.end());
    fill(out, {});
    return out;
}

InstallLocations default_install_at(std::string_view foc) {
    const std::vector<std::string> roots = default_steam_roots();
    return install_at(foc, roots);
}


// ---- #342 ---------------------------------------------------------------------------------------------------------------

namespace {

// A REG_SZ value as UTF-8 (read only); "" when absent.
std::string reg_text(HKEY root, const std::wstring& subkey, const wchar_t* value, REGSAM view) {
    HKEY k = nullptr;
    if (RegOpenKeyExW(root, subkey.c_str(), 0, KEY_READ | view, &k) != ERROR_SUCCESS) return {};
    DWORD bytes = 0;
    std::wstring w;
    if (RegGetValueW(k, nullptr, value, RRF_RT_REG_SZ, nullptr, nullptr, &bytes) == ERROR_SUCCESS && bytes > sizeof(wchar_t)) {
        w.resize(bytes / sizeof(wchar_t));
        if (RegGetValueW(k, nullptr, value, RRF_RT_REG_SZ, nullptr, w.data(), &bytes) != ERROR_SUCCESS) w.clear();
        while (!w.empty() && w.back() == L'\0') w.pop_back();
    }
    RegCloseKey(k);
    return w.empty() ? std::string() : native_utf8(fs::path(w));
}

// The subkey names of root\subkey (read only), sorted: the registry's enumeration order must not decide the result.
std::vector<std::wstring> reg_subkeys(HKEY root, const wchar_t* subkey, REGSAM view) {
    std::vector<std::wstring> out;
    HKEY k = nullptr;
    if (RegOpenKeyExW(root, subkey, 0, KEY_READ | view, &k) != ERROR_SUCCESS) return out;
    for (DWORD i = 0;; ++i) {
        wchar_t name[256];
        DWORD n = 256;
        if (RegEnumKeyExW(k, i, name, &n, nullptr, nullptr, nullptr, nullptr) != ERROR_SUCCESS) break;
        out.emplace_back(name, n);
    }
    RegCloseKey(k);
    std::sort(out.begin(), out.end());
    return out;
}

bool reparse_point(const fs::path& p) {
    const DWORD a = GetFileAttributesW(long_path(p).c_str());
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
}

// The likely folder names, in the order the scan takes them (before every other name).
constexpr std::string_view kLikely[] = {
    "Star Wars Empire at War",  // [name-ok: the game's install folder, not a faction]
    "corruption", "SteamLibrary", "steamapps", "common", "GOG Games", "Games", "Program Files", "Program Files (x86)"};
constexpr std::string_view kSkip[] = {"Windows", "Windows.old", "$Recycle.Bin", "System Volume Information", "Recovery",
                                      "$WinREAgent", "ProgramData", "AppData", "node_modules", ".git"};

size_t likely_rank(std::string_view name) {
    for (size_t i = 0; i < std::size(kLikely); ++i)
        if (ascii_iequals(name, kLikely[i])) return i;
    return std::size(kLikely);
}

bool skipped(std::string_view name) {
    for (const std::string_view s : kSkip)
        if (ascii_iequals(name, s)) return true;
    return false;
}

}  // namespace

std::vector<std::string> registry_install_folders() {
    std::vector<std::string> out, keys;
    const auto add = [&](std::string dir) {
        if (dir.empty() || !dir_exists(path_from_utf8(dir))) return;
        std::string key = lexical_folder_key(dir);
        if (std::find(keys.begin(), keys.end(), key) != keys.end()) return;
        keys.push_back(std::move(key));
        out.push_back(std::move(dir));
    };
    for (const HKEY root : {HKEY_LOCAL_MACHINE, HKEY_CURRENT_USER})
        for (const REGSAM view : {static_cast<REGSAM>(KEY_WOW64_64KEY), static_cast<REGSAM>(KEY_WOW64_32KEY)}) {
            for (const std::wstring& id : reg_subkeys(root, L"SOFTWARE\\GOG.com\\Games", view))
                add(reg_text(root, L"SOFTWARE\\GOG.com\\Games\\" + id, L"path", view));
            // [name-ok: the retail installer's registry keys, not factions]
            add(reg_text(root, L"SOFTWARE\\LucasArts\\Star Wars Empire at War Forces of Corruption\\1.0", L"InstallPath", view));
            add(reg_text(root, L"SOFTWARE\\LucasArts\\Star Wars Empire at War\\1.0", L"InstallPath", view));
        }
    return out;
}

std::vector<std::string> default_fixed_install_folders() {
    // [name-ok: GOG's default install folders of the game, not factions]
    return {"C:\\GOG Games\\Star Wars - Empire at War\\corruption",
            "C:\\Program Files (x86)\\GOG Galaxy\\Games\\Star Wars - Empire at War\\corruption"};
}

std::vector<std::string> fixed_drive_roots() {
    std::vector<std::string> out;
    const DWORD mask = GetLogicalDrives();
    for (int i = 0; i < 26; ++i) {
        if ((mask & (1u << i)) == 0) continue;
        const wchar_t root[4] = {static_cast<wchar_t>(L'A' + i), L':', L'\\', L'\0'};
        if (GetDriveTypeW(root) == DRIVE_FIXED) out.push_back(std::string(1, static_cast<char>('A' + i)) + ":\\");
    }
    return out;
}

FocScanResult scan_for_foc(const FocScan& scan) {
    FocScanResult r;
    struct Node {
        fs::path dir;
        uint32_t depth;
    };
    std::deque<Node> queue;
    for (const std::string& root : scan.roots)
        if (!root.empty()) queue.push_back({path_from_utf8(root), 0});
    while (!queue.empty()) {
        const Node n = std::move(queue.front());
        queue.pop_front();
        if (r.checked >= scan.max_folders) {
            r.limit_hit = true;
            return r;
        }
        ++r.checked;
        const std::string at = native_utf8(n.dir);
        if (scan.progress && scan.progress_every > 0 && r.checked % scan.progress_every == 0) scan.progress(r.checked, at);
        const std::string found = normalize_foc_dir(at);  // the folder itself, or its "corruption" folder
        if (!found.empty()) {
            r.foc = found;
            return r;
        }
        if (n.depth >= scan.max_depth) continue;
        std::vector<std::pair<std::string, fs::path>> children;  // (name, folder)
        std::error_code ec;
        for (fs::directory_iterator it(long_path(n.dir), fs::directory_options::skip_permission_denied, ec), end; !ec && it != end;
             it.increment(ec)) {
            std::error_code e2;
            if (!it->is_directory(e2)) continue;
            fs::path child = n.dir / it->path().filename();
            std::string name = native_utf8(child.filename());
            if (name.empty() || skipped(name) || reparse_point(child)) continue;
            children.emplace_back(std::move(name), std::move(child));
        }
        std::sort(children.begin(), children.end(), [](const auto& a, const auto& b) {
            const size_t ra = likely_rank(a.first), rb = likely_rank(b.first);
            if (ra != rb) return ra < rb;
            if (!ascii_iequals(a.first, b.first)) return ascii_iless(a.first, b.first);
            return a.first < b.first;
        });
        for (auto& c : children) queue.push_back({std::move(c.second), n.depth + 1});
    }
    return r;
}

}  // namespace alamo::vfs
