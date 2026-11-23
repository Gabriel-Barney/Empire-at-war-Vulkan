// #137 [port]: UTF-8 host strings (host_text.hpp).
#include "alamo/game/host_text.hpp"

#include <cstdlib>
#include <exception>
#include <string>

#include "alamo/core/host_fs.hpp"

#include <windows.h>

#include <shellapi.h>

namespace alamo::game {

namespace fs = std::filesystem;

std::string env_utf8(const char* name) {
    std::wstring wname;
    for (const char* c = name; *c; ++c) wname.push_back(static_cast<wchar_t>(static_cast<unsigned char>(*c)));  // ASCII names
    wchar_t* v = nullptr;
    size_t len = 0;
    std::wstring value;
    if (_wdupenv_s(&v, &len, wname.c_str()) == 0 && v) value = v;
    std::free(v);
    return value.empty() ? std::string() : host_utf8(fs::path(value));
}

fs::path host_path(std::string_view utf8) { return path_from_host_string(utf8); }

std::string host_utf8(const fs::path& p) {
    try {
        const std::u8string s = p.u8string();
        return std::string(reinterpret_cast<const char*>(s.data()), s.size());
    } catch (const std::exception&) {
        return {};
    }
}

std::vector<std::string> utf8_command_line(std::wstring_view command_line) {
    std::vector<std::string> out;
    const std::wstring line(command_line);
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(line.c_str(), &argc);
    if (!argv) return out;
    for (int i = 0; i < argc; ++i) {  // as typed: WideCharToMultiByte, not a path conversion
        const int len = WideCharToMultiByte(CP_UTF8, 0, argv[i], -1, nullptr, 0, nullptr, nullptr);
        std::string s(len > 0 ? static_cast<size_t>(len) : size_t{1}, '\0');
        if (len > 0) WideCharToMultiByte(CP_UTF8, 0, argv[i], -1, s.data(), len, nullptr, nullptr);
        s.resize(s.size() - 1);  // the terminator
        out.push_back(std::move(s));
    }
    LocalFree(argv);
    return out;
}

std::vector<std::string> utf8_args(int argc, char** argv) {
    std::vector<std::string> out = utf8_command_line(GetCommandLineW());
    if (!out.empty()) return out;
    for (int i = 0; i < argc; ++i) out.emplace_back(argv[i]);
    return out;
}

}  // namespace alamo::game
