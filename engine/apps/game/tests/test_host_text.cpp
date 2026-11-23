// alamo_game_host_text_tests (#137, data-free): alamo_game's UTF-8 host strings (host_text.hpp).
//   * utf8_command_line: a wide command line split as the C runtime splits it (CommandLineToArgvW), each argument UTF-8;
//   * host_path / host_utf8: UTF-8 <-> path, a string that is not UTF-8 read in the ANSI code page;
//   * env_utf8: the wide environment value as UTF-8.
// The names use Hebrew and CJK letters, which no single ANSI code page holds. Expected UTF-8 bytes are written out by hand
// (U+05D0 = D7 90, U+4E2D = E4 B8 AD).
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

#include "alamo/game/host_text.hpp"
#include "alamo/test/support.hpp"

#include <windows.h>

using ::alamo::test::g_fail;
namespace game = alamo::game;
namespace fs = std::filesystem;

namespace {

void command_line() {
    const std::vector<std::string> a =
        game::utf8_command_line(L"alamo_game.exe --saves-dir \"C:\\Users\\\u05D0\u4E2D\\Saves\" MODPATH=C:\\Mods\\x --seed 7");
    const std::vector<std::string> want = {"alamo_game.exe", "--saves-dir", "C:\\Users\\\xD7\x90\xE4\xB8\xAD\\Saves",
                                           "MODPATH=C:\\Mods\\x", "--seed", "7"};
    CHECK(a == want);
    // quotes inside an argument and an empty argument, as the C runtime reads them
    CHECK((game::utf8_command_line(L"x.exe \"a b\" \"\" c\\\"d") == std::vector<std::string>{"x.exe", "a b", "", "c\"d"}));
}

void paths() {
    CHECK(game::host_path("C:\\x\\\xD7\x90").native() == std::wstring(L"C:\\x\\\u05D0"));
    CHECK(game::host_utf8(fs::path(L"C:\\x\\\u05D0\u4E2D")) == "C:\\x\\\xD7\x90\xE4\xB8\xAD");
    CHECK(game::host_path("caf\xE9") == fs::path(std::string("caf\xE9")));  // not UTF-8: the ANSI code page
    CHECK(game::host_path("").empty());
}

void environment() {
    CHECK(_wputenv_s(L"ALAMO_T137_HOST_TEXT", L"a\u05D0") == 0);
    CHECK(game::env_utf8("ALAMO_T137_HOST_TEXT") == "a\xD7\x90");
    CHECK(_wputenv_s(L"ALAMO_T137_HOST_TEXT", L"") == 0);  // removes it
    CHECK(game::env_utf8("ALAMO_T137_HOST_TEXT").empty());
}

}  // namespace

int main() {
    command_line();
    paths();
    environment();
    std::printf(g_fail ? "alamo_game_host_text_tests: %d FAILED\n" : "alamo_game_host_text_tests: OK\n", g_fail);
    return g_fail ? 1 : 0;
}
