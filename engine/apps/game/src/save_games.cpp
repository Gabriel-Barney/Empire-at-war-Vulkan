// N1 / t09 (owner: n2-game): saved games on disk (save_games.hpp).
#include "alamo/game/save_games.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <system_error>

#include "alamo/core/ascii.hpp"
#include "alamo/core/bytes.hpp"
#include "alamo/core/host_fs.hpp"
#include "alamo/core/utf8.hpp"
#include "alamo/game/host_text.hpp"


namespace alamo::game {

namespace fs = std::filesystem;

namespace {

// every path string here is UTF-8 (the dialogs' text): std::string <-> fs::path through host_path / host_utf8
// (host_text.hpp).

bool read_bytes(const std::string& path, std::vector<uint8_t>& out, size_t max_bytes) {
    std::ifstream f(host_path(path), std::ios::binary);
    if (!f) return false;
    if (max_bytes == 0) {
        f.seekg(0, std::ios::end);
        const std::streamoff size = f.tellg();
        if (size < 0) return false;
        out.resize(static_cast<size_t>(size));
        f.seekg(0);
    } else {
        out.resize(max_bytes);
    }
    f.read(reinterpret_cast<char*>(out.data()), static_cast<std::streamsize>(out.size()));
    if (max_bytes == 0 && !f) return false;
    out.resize(static_cast<size_t>(std::max<std::streamsize>(0, f.gcount())));
    return true;
}

constexpr const char* kMarker = "alamo_game_save 1";
constexpr uint32_t kHeaderFormat = 1;          // the SaveFile layout read_save_header understands without a full decode
constexpr size_t kHeaderBytes = 64u * 1024u;   // description longer than this: full read

// Save list order: lower-cased bytes compared as char. Not ascii_iless (upper-cased unsigned bytes), which would sort '_'
// and bytes >= 0x80 differently against letters.
bool iless(std::string_view a, std::string_view b) {
    const size_t n = std::min(a.size(), b.size());
    for (size_t i = 0; i < n; ++i) {
        const char x = ascii_to_lower(a[i]), y = ascii_to_lower(b[i]);
        if (x != y) return x < y;
    }
    return a.size() < b.size();
}

bool fail(std::string* err, std::string m) {
    if (err) *err = std::move(m);
    return false;
}

// One description value on one line ('\n' / '\r' would end it).
std::string one_line(std::string_view v) {
    std::string s(v);
    for (char& c : s)
        if (c == '\n' || c == '\r') c = ' ';
    return s;
}

int64_t to_i64(std::string_view v, int64_t fallback) {
    if (v.empty()) return fallback;
    const std::string s(v);
    char* end = nullptr;
    const long long r = std::strtoll(s.c_str(), &end, 10);
    return end && *end == '\0' ? static_cast<int64_t>(r) : fallback;
}

int64_t file_time_seconds(const fs::path& p) {
    std::error_code ec;
    const fs::file_time_type t = fs::last_write_time(p, ec);
    if (ec) return 0;
    const auto sys = std::chrono::clock_cast<std::chrono::system_clock>(t);
    return static_cast<int64_t>(std::chrono::duration_cast<std::chrono::seconds>(sys.time_since_epoch()).count());
}

bool reserved_device(std::string_view s) {
    static constexpr const char* kNames[] = {"CON", "PRN", "AUX", "NUL"};
    for (const char* n : kNames)
        if (ascii_iequals(s, n)) return true;
    if (s.size() == 4 && (ascii_iequals(s.substr(0, 3), "COM") || ascii_iequals(s.substr(0, 3), "LPT")) && s[3] >= '1' && s[3] <= '9') return true;
    return false;
}

}  // namespace

std::string default_saves_dir() {
    // the wide value (env_utf8), so a profile folder outside the ANSI code page keeps its name.
    const std::string la = env_utf8("LOCALAPPDATA");
    if (la.empty()) return {};
    return host_utf8(host_path(la) / "alamo" / "Saves");
}

std::string format_local_time(int64_t unix_seconds) {
    if (unix_seconds <= 0) return {};
    const std::time_t t = static_cast<std::time_t>(unix_seconds);
    std::tm tm{};
    if (localtime_s(&tm, &t) != 0) return {};
    char buf[32];
    std::snprintf(buf, sizeof buf, "%04d-%02d-%02d %02d:%02d", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min);
    return buf;
}

std::string screenshot_file_name(int year, int month, int day, int hour, int minute, int second, int centis) {
    char buf[48];
    std::snprintf(buf, sizeof buf, "EAW_%04d%02d%02d_%02d%02d%02d%02d.png", year, month, day, hour, minute, second, centis);
    return buf;
}

std::string screenshot_file_name_now() {
    const auto now = std::chrono::system_clock::now();
    const std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
    if (localtime_s(&tm, &t) != 0) return screenshot_file_name(1970, 1, 1, 0, 0, 0, 0);
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count() % 1000;
    return screenshot_file_name(tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec,
                                static_cast<int>(ms / 10));
}

int64_t unix_now() {
    const auto now = std::chrono::system_clock::now().time_since_epoch();
    return static_cast<int64_t>(std::chrono::duration_cast<std::chrono::seconds>(now).count());
}

std::string save_label(const SaveMeta& m) {
    std::string s = m.name.empty() ? std::string("(unnamed)") : m.name;
    std::string what = m.title.empty() ? m.campaign : m.title;
    if (m.kind == "skirmish") what = "Skirmish " + what;
    if (!m.faction.empty()) what += (what.empty() ? "" : " ") + std::string("(") + m.faction + ")";
    std::string when;
    if (m.week > 0) {
        when = "week " + std::to_string(m.week);
    } else if (m.game_seconds > 0 || m.kind == "skirmish") {
        char buf[32];
        std::snprintf(buf, sizeof buf, "%llu:%02llu", static_cast<unsigned long long>(m.game_seconds / 60u),
                      static_cast<unsigned long long>(m.game_seconds % 60u));
        when = buf;
    }
    const std::string at = format_local_time(m.saved_at);
    std::string rest = what;
    const std::string* const parts[] = {&when, &at};
    for (const std::string* part : parts)
        if (!part->empty()) rest += (rest.empty() ? "" : ", ") + *part;
    if (!rest.empty()) s += "  -  " + rest;
    return s;
}

std::string save_row_label(const SaveMeta& m, const SaveRowWords& words) {
    std::string s = m.name.empty() ? words.unnamed : m.name;
    std::string when;
    if (m.week > 0) {
        when = words.week + std::to_string(m.week);
    } else if (m.game_seconds > 0 || m.kind == "skirmish") {
        char buf[32];
        std::snprintf(buf, sizeof buf, "%llu:%02llu", static_cast<unsigned long long>(m.game_seconds / 60u),
                      static_cast<unsigned long long>(m.game_seconds % 60u));
        when = buf;
    }
    const std::string at = format_local_time(m.saved_at);
    std::string rest = when;
    if (!at.empty()) rest += (rest.empty() ? "" : ", ") + at;
    if (!rest.empty()) s += "  -  " + rest;
    return s;
}

std::string new_playthrough_id(int64_t now) {
    // Presentation only (never read by the sim): the wall clock plus the high-resolution clock's low bits, so two games
    // started in the same second still differ.
    const auto hr = static_cast<uint64_t>(std::chrono::high_resolution_clock::now().time_since_epoch().count());
    char buf[48];
    std::snprintf(buf, sizeof buf, "%llx-%08llx", static_cast<unsigned long long>(now),
                  static_cast<unsigned long long>((hr ^ (hr >> 32)) & 0xffffffffull));
    return buf;
}

std::string encode_description(const SaveMeta& m) {
    std::string s = one_line(save_label(m));
    s += "\n";
    s += kMarker;
    auto kv = [&s](const char* k, std::string_view v) {
        s += "\n";
        s += k;
        s += "=";
        s += one_line(v);
    };
    kv("name", m.name);
    kv("kind", m.kind);
    kv("campaign", m.campaign);
    kv("title", m.title);
    kv("faction", m.faction);
    kv("difficulty", std::to_string(m.difficulty));
    kv("tick", std::to_string(m.tick));
    kv("week", std::to_string(m.week));
    kv("game_seconds", std::to_string(m.game_seconds));
    kv("saved_at", std::to_string(m.saved_at));
    kv("faction_icon", m.faction_icon);
    kv("playthrough", m.playthrough);
    kv("playthrough_started", std::to_string(m.playthrough_started));
    return s;
}

bool decode_description(std::string_view text, SaveMeta& out) {
    std::vector<std::string_view> lines;
    for (size_t p = 0; p <= text.size();) {
        const size_t e = text.find('\n', p);
        const size_t end = e == std::string_view::npos ? text.size() : e;
        std::string_view l = text.substr(p, end - p);
        if (!l.empty() && l.back() == '\r') l.remove_suffix(1);
        lines.push_back(l);
        if (e == std::string_view::npos) break;
        p = e + 1;
    }
    size_t at = lines.size();
    for (size_t i = 0; i < lines.size(); ++i)
        if (lines[i] == kMarker) {
            at = i;
            break;
        }
    if (at == lines.size()) return false;
    SaveMeta m;
    for (size_t i = at + 1; i < lines.size(); ++i) {
        const size_t eq = lines[i].find('=');
        if (eq == std::string_view::npos) continue;
        const std::string_view k = lines[i].substr(0, eq), v = lines[i].substr(eq + 1);
        if (k == "name") m.name = std::string(v);
        else if (k == "kind") m.kind = std::string(v);
        else if (k == "campaign") m.campaign = std::string(v);
        else if (k == "title") m.title = std::string(v);
        else if (k == "faction") m.faction = std::string(v);
        else if (k == "difficulty") m.difficulty = static_cast<int32_t>(std::clamp<int64_t>(to_i64(v, 1), 0, 2));
        else if (k == "tick") m.tick = static_cast<uint64_t>(std::max<int64_t>(0, to_i64(v, 0)));
        else if (k == "week") m.week = static_cast<uint32_t>(std::clamp<int64_t>(to_i64(v, 0), 0, 0x7fffffff));
        else if (k == "game_seconds") m.game_seconds = static_cast<uint64_t>(std::max<int64_t>(0, to_i64(v, 0)));
        else if (k == "saved_at") m.saved_at = to_i64(v, 0);
        else if (k == "faction_icon") m.faction_icon = std::string(v);
        else if (k == "playthrough") m.playthrough = std::string(v);
        else if (k == "playthrough_started") m.playthrough_started = to_i64(v, 0);
    }
    out = std::move(m);
    return true;
}

std::string sanitize_save_name(std::string_view typed) {
    std::string s;
    for (size_t i = 0; i < typed.size();) {
        const char c = typed[i];
        const auto u = static_cast<unsigned char>(c);
        if (u >= 0x80) {  // a valid UTF-8 character is kept (Windows names are Unicode); a broken byte becomes '_'
            const size_t n = alamo::utf8_sequence(typed, i);  // core/utf8.hpp
            if (n == 0) {
                s.push_back('_');
                ++i;
            } else {
                s.append(typed.substr(i, n));
                i += n;
            }
            continue;
        }
        const bool bad = u < 0x20 || u == 0x7F || c == '<' || c == '>' || c == ':' || c == '"' || c == '/' || c == '\\' || c == '|' ||
                         c == '?' || c == '*';
        s.push_back(bad ? '_' : c);
        ++i;
    }
    auto trim = [](char c) { return c == ' ' || c == '.'; };
    while (!s.empty() && trim(s.front())) s.erase(s.begin());
    while (!s.empty() && trim(s.back())) s.pop_back();
    if (s.size() > kMaxSaveName) {  // bytes; cut at a character boundary
        size_t cut = kMaxSaveName;
        while (cut > 0 && (static_cast<unsigned char>(s[cut]) & 0xC0u) == 0x80u) --cut;
        s.resize(cut);
    }
    while (!s.empty() && trim(s.back())) s.pop_back();
    if (reserved_device(s)) s.push_back('_');
    return s;
}

std::string save_path(const std::string& dir, std::string_view name) {
    const std::string stem = sanitize_save_name(name);
    if (stem.empty()) return {};
    // A listed file is found by its own stem even when sanitizing would change it (a name over kMaxSaveName bytes copied
    // in by hand): the dialogs hand back SaveEntry::name, so load / delete must reach that file.
    // Only a plain file name: no separator, drive / stream colon or other character Windows forbids, no control byte.
    const bool plain = name.find_first_of("<>:\"/\\|?*") == std::string_view::npos && name.find("..") == std::string_view::npos &&
                       std::none_of(name.begin(), name.end(), [](char c) { return static_cast<unsigned char>(c) < 0x20; });
    if (stem != name && plain) {
        std::error_code ec;
        const fs::path own = host_path(dir) / host_path(std::string(name) + kSaveExtension);
        if (fs::is_regular_file(own, ec)) return host_utf8(own);
    }
    return host_utf8(host_path(dir) / host_path(stem + kSaveExtension));
}

bool save_exists(const std::string& dir, std::string_view name) {
    const std::string p = save_path(dir, name);
    std::error_code ec;
    return !p.empty() && fs::is_regular_file(host_path(p), ec);
}

bool delete_save(const std::string& dir, std::string_view name, std::string* err) {
    const std::string p = save_path(dir, name);
    if (p.empty()) return fail(err, "no save name");
    std::error_code ec;
    if (!fs::remove(host_path(p), ec)) return fail(err, ec ? "cannot delete " + p + ": " + ec.message() : "no saved game " + p);
    return true;
}

bool write_save_file(const SaveFile& save, const std::string& path, std::string* err) {
    if (path.empty()) return fail(err, "no save file name");
    const fs::path target = host_path(path);
    std::error_code ec;
    if (target.has_parent_path()) {
        fs::create_directories(target.parent_path(), ec);
        if (ec) return fail(err, "cannot create " + host_utf8(target.parent_path()) + ": " + ec.message());
    }
    const fs::path tmp = host_path(path + ".tmp");
    {
        const std::vector<uint8_t> bytes = save.encode();
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (f) f.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        if (f) f.close();  // a failed flush is a failed write too: never rename a truncated file over the save
        if (!f) {
            f.close();
            fs::remove(tmp, ec);
            return fail(err, "cannot write " + path + ".tmp");
        }
    }
    fs::rename(tmp, target, ec);  // replaces an existing save in one step
    if (ec) {
        std::error_code ec2;
        fs::remove(tmp, ec2);
        return fail(err, "cannot write " + path + ": " + ec.message());
    }
    return true;
}

bool read_save_file(const std::string& path, SaveFile& out, std::string* err) {
    std::vector<uint8_t> bytes;
    if (!read_bytes(path, bytes, 0)) return fail(err, "cannot open " + path);
    return SaveFile::decode(bytes, out, err);
}

bool read_save_header(const std::string& path, uint64_t& tick, std::string& description, std::string* err) {
    {
        std::vector<uint8_t> head;
        if (!read_bytes(path, head, kHeaderBytes)) return fail(err, "cannot open " + path);
        ByteReader r(head);
        const uint32_t magic = r.u32(), version = r.u32();
        if (!r.ok() || magic != kSaveMagic) return fail(err, "not a saved game");
        if (version == kHeaderFormat) {
            const uint64_t t = r.u64();
            std::string d = r.str();
            if (r.ok()) {
                tick = t;
                description = std::move(d);
                return true;
            }
        }
    }
    SaveFile s;  // another layout (or a very long description): the full decode
    if (!read_save_file(path, s, err)) return false;
    tick = s.tick;
    description = s.description;
    return true;
}

std::vector<SaveEntry> list_saves(const std::string& dir) {
    std::vector<SaveEntry> out;
    std::error_code ec;
    if (dir.empty()) return out;
    const fs::path d = host_path(dir);
    if (!fs::is_directory(d, ec)) return out;
    for (fs::directory_iterator it(d, ec), end; !ec && it != end; it.increment(ec)) {
        const fs::path& p = it->path();
        std::error_code ec2;
        if (!it->is_regular_file(ec2) || !ascii_iequals(host_utf8(p.extension()), kSaveExtension)) continue;
        SaveEntry e;
        e.name = host_utf8(p.stem());
        e.path = host_utf8(p);
        if (e.name.empty() || e.path.empty()) continue;  // no UTF-8 form: not a name the dialogs could show or open
        std::string desc;
        e.readable = read_save_header(e.path, e.tick, desc, &e.error);
        if (e.readable && !decode_description(desc, e.meta)) {
            e.meta = SaveMeta{};  // an engine save without the game's description (e.g. alamo_headless)
            e.meta.title = desc.substr(0, desc.find('\n'));
        }
        if (e.meta.name.empty()) e.meta.name = e.name;
        if (e.meta.tick == 0) e.meta.tick = e.tick;
        e.sort_time = e.meta.saved_at > 0 ? e.meta.saved_at : file_time_seconds(p);
        out.push_back(std::move(e));
    }
    std::sort(out.begin(), out.end(), [](const SaveEntry& a, const SaveEntry& b) {
        if (a.sort_time != b.sort_time) return a.sort_time > b.sort_time;
        if (!ascii_iequals(a.name, b.name)) return iless(a.name, b.name);
        return a.name < b.name;
    });
    return out;
}

}  // namespace alamo::game
