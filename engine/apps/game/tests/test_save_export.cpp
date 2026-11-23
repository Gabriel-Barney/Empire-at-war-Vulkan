// Launcher support (save_export.hpp): a campaign saved through the menus' save path, exported to JSON through the load
// path, on the real FoC data (ALAMO_FOC_DATA; skipped when unset). Checks: the export loads the save and matches the
// session's galaxy (every planet row in order, routes, live fleets, the human player's faction and credits), the JSON is
// well formed (brackets balance outside strings, no raw control characters), the same session exports the same text,
// and a file that is not a save is refused without writing the output. With --exe ALAMO_GAME (#137 t02): alamo_game
// --export-save on a save in a folder named outside the ANSI code page.
#include <cinttypes>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "alamo/galactic/campaign.hpp"
#include "alamo/game/game_data.hpp"
#include "alamo/game/game_session.hpp"
#include "alamo/game/save_export.hpp"
#include "alamo/game/save_games.hpp"

#include <windows.h>

using namespace alamo;
namespace fs = std::filesystem;

static int g_fail = 0;
#define CHECK(c)                                                     \
    do {                                                             \
        if (!(c)) {                                                  \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); \
            ++g_fail;                                                \
        }                                                            \
    } while (0)

namespace {

std::string read_all(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

// Brackets balance outside strings, strings close, no raw control characters inside them.
bool well_formed(const std::string& s) {
    std::vector<char> stack;
    bool in_str = false, esc = false;
    for (const char c : s) {
        if (in_str) {
            if (esc) esc = false;
            else if (c == '\\') esc = true;
            else if (c == '"') in_str = false;
            else if (static_cast<unsigned char>(c) < 0x20) return false;
            continue;
        }
        if (c == '"') in_str = true;
        else if (c == '{' || c == '[') stack.push_back(c);
        else if (c == '}' || c == ']') {
            if (stack.empty() || stack.back() != (c == '}' ? '{' : '[')) return false;
            stack.pop_back();
        }
    }
    return !in_str && stack.empty();
}

size_t count(const std::string& s, const std::string& what) {
    size_t n = 0;
    for (size_t at = s.find(what); at != std::string::npos; at = s.find(what, at + what.size())) ++n;
    return n;
}

std::string quoted(const std::string& s) { return "\"" + s + "\""; }

}  // namespace

int main(int argc, char** argv) {
    const char* exe_arg = argc == 3 && std::string(argv[1]) == "--exe" ? argv[2] : nullptr;  // #137 t02
    const char* foc = std::getenv("ALAMO_FOC_DATA");
    if (!foc || !*foc) {
        std::printf("alamo_game_save_export_tests: ALAMO_FOC_DATA not set: skipped\n");
        return 0;
    }
    game::GameData data;
    std::string err;
    if (!game::load_game_data(foc, {}, data, &err)) {
        std::printf("data load failed: %s\n", err.c_str());
        return 1;
    }
    const fs::path dir = fs::temp_directory_path() / "alamo_game_save_export_tests";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir, ec);
    jobs::JobSystem js(jobs::JobSystemConfig{2});

    game::SessionSpec spec;
    spec.kind = game::SessionKind::Campaign;
    spec.campaign = "Sandbox_Heart_Of_The_Maw_Empire";
    spec.faction = "Empire";
    spec.seed = 7;
    spec.animation = false;
    game::GameSession s(data, js, spec);
    std::vector<std::string> diag;
    CHECK(s.start(&diag));
    for (int i = 0; i < 200; ++i) s.tick();
    std::string save_path;
    CHECK(game::save_game(s, (dir / "Saves").string(), "Export check", "Heart of the Maw", 1794146580, &save_path, &err));
    if (save_path.empty()) {
        std::printf("save failed: %s\n", err.c_str());
        return 1;
    }

    const std::vector<game::ExportColor> colors = {{"Empire", 200, 30, 30}};
    const fs::path out = dir / "export.json";
    CHECK(game::export_save(data, js, save_path, out.string(), colors, &err));
    const std::string json = read_all(out);
    std::printf("export: %zu bytes\n", json.size());
    CHECK(json.rfind("{\"format\":1,", 0) == 0);
    CHECK(well_formed(json));

    // The loaded session's galaxy, as the export saw it.
    SaveFile file;
    std::unique_ptr<game::GameSession> loaded = game::load_game(data, js, save_path, &err, nullptr, nullptr, &file);
    CHECK(loaded != nullptr);
    if (loaded) {
        const galactic::GalacticSim* g = loaded->galaxy();
        CHECK(g != nullptr);
        if (g) {
            const auto& map = g->map();
            // Every planet row, in row order.
            size_t at = json.find("\"planets\":[{");
            CHECK(at != std::string::npos);
            for (size_t i = 0; i < map.planets.size() && at != std::string::npos; ++i) {
                at = json.find("\"name\":" + quoted(map.planets.name[i]), at);
                CHECK(at != std::string::npos);
            }
            CHECK(count(json, "{\"a\":") == map.routes.size());
            size_t live = 0;
            for (const auto& f : g->fleets()) live += f.alive ? 1 : 0;
            CHECK(count(json, "\"moving\":") == live);
            CHECK(count(json, "\"credits\":") == g->players().size());
            std::printf("planets %zu, routes %zu, fleets %zu, players %zu, local %d\n", map.planets.size(), map.routes.size(), live,
                        g->players().size(), g->local_player());
        }
        CHECK(json.find("\"faction\":\"Empire\",\"color\":\"#c81e1e\",\"human\":true") != std::string::npos);
        CHECK(json.find("\"title\":\"Heart of the Maw\"") != std::string::npos);
        CHECK(json.find("\"name\":\"Export check\"") != std::string::npos);
        // Deterministic: the same session exports the same text.
        std::string again;
        CHECK(game::export_session_json(data, *loaded, file, colors, again, &err) && again == json);
    }

    // #137 t02 (--exe ALAMO_GAME): alamo_game --export-save on a save in a folder named outside the ANSI code page
    // (Hebrew and CJK, which no single code page holds), the arguments passed wide (CreateProcessW) and the output
    // written there too: the arguments reach the game as UTF-8 and the export opens and writes those paths.
    if (exe_arg) {
        const fs::path udir = dir / L"אב_中文";
        fs::create_directories(udir, ec);
        const fs::path usave = udir / L"א.alsv", uout = udir / L"中.json";
        fs::copy_file(fs::path(save_path), usave, ec);
        CHECK(!ec);
        std::wstring line = L"\"" + fs::path(exe_arg).wstring() + L"\" --export-save \"" + usave.wstring() + L"\" --out \"" +
                            uout.wstring() + L"\" --game \"" + fs::path(foc).wstring() + L"\"";
        STARTUPINFOW si{};
        si.cb = sizeof si;
        PROCESS_INFORMATION pi{};
        DWORD code = 99;
        if (CreateProcessW(nullptr, line.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi)) {
            WaitForSingleObject(pi.hProcess, 300000);
            GetExitCodeProcess(pi.hProcess, &code);
            CloseHandle(pi.hThread);
            CloseHandle(pi.hProcess);
        }
        const std::string ujson = read_all(uout);
        std::printf("alamo_game --export-save outside the ANSI code page: exit %lu, %zu bytes\n", static_cast<unsigned long>(code),
                    ujson.size());
        CHECK(code == 0);
        CHECK(ujson.rfind("{\"format\":1,", 0) == 0 && well_formed(ujson));
        CHECK(ujson.find("\"name\":\"Export check\"") != std::string::npos);
    }

    // Not a save: refused, nothing written.
    const fs::path junk = dir / "junk.alsv";
    std::ofstream(junk, std::ios::binary) << "not a save";
    const fs::path junk_out = dir / "junk.json";
    err.clear();
    CHECK(!game::export_save(data, js, junk.string(), junk_out.string(), colors, &err));
    CHECK(!err.empty() && !fs::exists(junk_out));
    std::printf("junk refused: %s\n", err.c_str());

    fs::remove_all(dir, ec);
    std::printf(g_fail ? "alamo_game_save_export_tests: %d FAILED\n" : "alamo_game_save_export_tests: OK\n", g_fail);
    return g_fail ? 1 : 0;
}
