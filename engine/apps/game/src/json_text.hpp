// the one JSON string escaper of alamo_game_core (internal header; mod_check.cpp's report and benchmark.cpp's
// bench_report_json). Other module-local escapers (apps/sim bench_stats.cpp, the materials tools, tools/formats
// json_writer.hpp) are candidates for one core helper, which would be a core header change.
#pragma once
#include <cstddef>
#include <cstdio>
#include <string>
#include <string_view>

#include "alamo/core/utf8.hpp"

namespace alamo::game::json_text {

// Appends `s` as a JSON string. Mod data and file names are not always UTF-8 (a mod's XML in a Windows code page): a byte
// that does not start a valid UTF-8 sequence is written as the Latin-1 character of that value (\u00XX), so the output is
// always valid JSON. '"', '\\' and control characters are escaped; valid UTF-8 passes through unchanged.
inline void json_str(std::string& o, std::string_view s) {
    o.push_back('"');
    for (size_t i = 0; i < s.size();) {
        const char c = s[i];
        const auto u = static_cast<unsigned char>(c);
        const size_t n = alamo::utf8_sequence(s, i);  // core/utf8.hpp: 0 = not well-formed UTF-8 here
        if (c == '"' || c == '\\') {
            o.push_back('\\');
            o.push_back(c);
        } else if (u < 0x20 || n == 0) {
            char buf[8];
            std::snprintf(buf, sizeof buf, "\\u%04x", static_cast<unsigned>(u));
            o += buf;
        } else {
            o.append(s.substr(i, n));
            i += n;
            continue;
        }
        ++i;
    }
    o.push_back('"');
}

}  // namespace alamo::game::json_text
