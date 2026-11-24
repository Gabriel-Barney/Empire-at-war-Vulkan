// UTF-8 validation (#138): the one well-formedness rule the engine's UTF-8 readers share (save names, fleet and ship
// names, the save export's JSON strings). Well-formed is RFC 3629: no overlong forms, no surrogates (U+D800..U+DFFF),
// nothing above U+10FFFF, no stray continuation bytes.
#pragma once
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace alamo {

// Length (1..4) of the well-formed UTF-8 sequence that starts at s[i], its code point in `cp`; 0 when there is none
// (`cp` is then unspecified). i must be < s.size().
constexpr size_t utf8_sequence(std::string_view s, size_t i, char32_t& cp) noexcept {
    const auto b = [&](size_t k) { return static_cast<uint8_t>(s[k]); };
    const uint8_t c = b(i);
    size_t n = 0;
    if (c < 0x80) {
        cp = c;
        return 1;
    }
    if (c >= 0xC2 && c <= 0xDF) n = 2, cp = c & 0x1Fu;
    else if (c >= 0xE0 && c <= 0xEF) n = 3, cp = c & 0x0Fu;
    else if (c >= 0xF0 && c <= 0xF4) n = 4, cp = c & 0x07u;
    else return 0;  // a stray continuation byte, an overlong lead (C0 / C1) or out of range (F5..FF)
    if (i + n > s.size()) return 0;
    for (size_t k = 1; k < n; ++k) {
        if ((b(i + k) & 0xC0u) != 0x80u) return 0;
        cp = (cp << 6) | (b(i + k) & 0x3Fu);
    }
    if ((n == 3 && cp < 0x800) || (n == 4 && (cp < 0x10000 || cp > 0x10FFFF))) return 0;  // overlong / out of range
    if (cp >= 0xD800 && cp <= 0xDFFF) return 0;                                        // surrogates
    return n;
}

constexpr size_t utf8_sequence(std::string_view s, size_t i) noexcept {
    char32_t cp = 0;
    return utf8_sequence(s, i, cp);
}

// The whole string is well-formed UTF-8 (an empty string is).
constexpr bool utf8_valid(std::string_view s) noexcept {
    for (size_t i = 0; i < s.size();) {
        const size_t n = utf8_sequence(s, i);
        if (n == 0) return false;
        i += n;
    }
    return true;
}

}  // namespace alamo
