// #137 [port]: alamo_game's host path strings are UTF-8 (the vfs's convention, alamo/core/host_fs.hpp path_from_utf8).
// std::string <-> std::filesystem::path goes through these, never through fs::path(std::string) / path::string(),
// which use the ANSI code page and throw on a name outside it.
#pragma once
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace alamo::game {

// The environment variable `name` as UTF-8 (Windows: the wide value, so a profile folder outside the ANSI code page
// keeps its name); "" when unset.
std::string env_utf8(const char* name);
// A host path from UTF-8. A string that is not valid UTF-8 (a path in the ANSI code page from an older caller) is read
// as ANSI instead of throwing; an empty path when it is not valid ANSI either. Never throws. (core path_from_host_string)
std::filesystem::path host_path(std::string_view utf8);
// UTF-8 with native separators ('\' on Windows), unlike path_to_utf8's generic form; "" for a name that has no UTF-8
// form (a lone UTF-16 surrogate), so the caller can skip it instead of throwing.
std::string host_utf8(const std::filesystem::path& p);

// #137 t02: a wide command line split by CommandLineToArgvW (the C runtime's rules, except a doubled "" inside quotes,
// which windows_command_line never writes), each argument as UTF-8 and as typed (no path conversion: a MODPATH= keeps
// its backslashes). Empty when it cannot be split.
std::vector<std::string> utf8_command_line(std::wstring_view command_line);
// The process's arguments as UTF-8, from the wide command line (GetCommandLineW), so a path outside the ANSI code page
// arrives whole; the narrow argv only when the wide line cannot be split.
std::vector<std::string> utf8_args(int argc, char** argv);

}  // namespace alamo::game
