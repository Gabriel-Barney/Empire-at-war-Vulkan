#include "alamo/game/open_game_data.hpp"

#include <cstdlib>
#include <exception>
#include <filesystem>
#include <string>
#include <utility>

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
