// #137 [port]: UTF-8 host strings (host_text.hpp).
#include "alamo/game/host_text.hpp"

#include <string>

#include "alamo/core/host_fs.hpp"

namespace alamo::game {

namespace fs = std::filesystem;

std::string env_utf8(const char* name) { return host_env_utf8(name); }  // #338: core host_fs

fs::path host_path(std::string_view utf8) { return path_from_host_string(utf8); }

std::string host_utf8(const fs::path& p) { return path_to_native_utf8(p); }

std::vector<std::string> utf8_command_line(std::wstring_view command_line) { return host_command_line_utf8(command_line); }

std::vector<std::string> utf8_args(int argc, char** argv) { return host_args_utf8(argc, argv); }

}  // namespace alamo::game
