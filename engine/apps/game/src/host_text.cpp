// #137 [port]: UTF-8 host strings (host_text.hpp).
#include "alamo/game/host_text.hpp"

#include <cstdlib>
#include <exception>
#include <string>

#include "alamo/core/host_fs.hpp"

namespace alamo::game {

namespace fs = std::filesystem;

std::string env_utf8(const char* name) {
#ifdef _WIN32
    std::wstring wname;
    for (const char* c = name; *c; ++c) wname.push_back(static_cast<wchar_t>(static_cast<unsigned char>(*c)));  // ASCII names
    wchar_t* v = nullptr;
    size_t len = 0;
    std::wstring value;
    if (_wdupenv_s(&v, &len, wname.c_str()) == 0 && v) value = v;
    std::free(v);
    return value.empty() ? std::string() : host_utf8(fs::path(value));
#else
    const char* v = std::getenv(name);
    return v ? std::string(v) : std::string();
#endif
}

fs::path host_path(std::string_view utf8) {
    try {
        return path_from_utf8(utf8);
    } catch (const std::exception&) {
        return fs::path(std::string(utf8));
    }
}

std::string host_utf8(const fs::path& p) {
    try {
        const std::u8string s = p.u8string();
        return std::string(reinterpret_cast<const char*>(s.data()), s.size());
    } catch (const std::exception&) {
        return {};
    }
}

}  // namespace alamo::game
