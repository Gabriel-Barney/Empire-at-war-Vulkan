// N1 save / load helpers shared by the AI save code (ai_save.cpp, galactic_ai_save.cpp, ai_lua_runtime.cpp, the tactical
// AI's tactical_save_io.hpp). Not installed. Byte rules of alamo/core/save_file.hpp: little-endian fields, counts before
// data, no pointers / size_t.
#pragma once
#include <concepts>
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <type_traits>
#include <vector>

#include "alamo/core/bytes.hpp"
#include "alamo/core/sim_crc.hpp"
#include "alamo/galactic/bitset.hpp"
#include "alamo/objects/object_store.hpp"

namespace alamo::ai::save {

template <class T>
inline constexpr bool kSupported =
    std::is_same_v<T, bool> || std::is_same_v<T, uint8_t> || std::is_same_v<T, int8_t> || std::is_same_v<T, int32_t> ||
    std::is_same_v<T, uint32_t> || std::is_same_v<T, int64_t> || std::is_same_v<T, uint64_t> || std::is_same_v<T, float> ||
    std::is_same_v<T, double> || std::is_same_v<T, std::string>;

template <class T>
void put(ByteWriter& w, const T& v) {
    static_assert(kSupported<T>);
    if constexpr (std::is_same_v<T, bool>) w.boolean(v);
    else if constexpr (std::is_same_v<T, uint8_t>) w.u8(v);
    else if constexpr (std::is_same_v<T, int8_t>) w.u8(static_cast<uint8_t>(v));
    else if constexpr (std::is_same_v<T, int32_t>) w.i32(v);
    else if constexpr (std::is_same_v<T, uint32_t>) w.u32(v);
    else if constexpr (std::is_same_v<T, int64_t>) w.i64(v);
    else if constexpr (std::is_same_v<T, uint64_t>) w.u64(v);
    else if constexpr (std::is_same_v<T, float>) w.f32(v);
    else if constexpr (std::is_same_v<T, double>) w.f64(v);
    else w.str(v);
}

template <class T>
T get(ByteReader& r) {
    static_assert(kSupported<T>);
    if constexpr (std::is_same_v<T, bool>) return r.boolean();
    else if constexpr (std::is_same_v<T, uint8_t>) return r.u8();
    else if constexpr (std::is_same_v<T, int8_t>) return static_cast<int8_t>(r.u8());
    else if constexpr (std::is_same_v<T, int32_t>) return r.i32();
    else if constexpr (std::is_same_v<T, uint32_t>) return r.u32();
    else if constexpr (std::is_same_v<T, int64_t>) return r.i64();
    else if constexpr (std::is_same_v<T, uint64_t>) return r.u64();
    else if constexpr (std::is_same_v<T, float>) return r.f32();
    else if constexpr (std::is_same_v<T, double>) return r.f64();
    else return r.str();
}

// Smallest encoded size of one T (count sanity check on corrupted data).
template <class T>
constexpr size_t min_size() {
    if constexpr (std::is_same_v<T, std::string>) return 4;
    else return sizeof(T);
}

// A count followed by at least `min_bytes` per element: rejected (r.fail()) when the rest of the data cannot hold it.
inline uint32_t count(ByteReader& r, size_t min_bytes) {
    const uint32_t n = r.u32();
    if (!r.ok()) return 0;
    if (min_bytes > 0 && static_cast<uint64_t>(n) * min_bytes > r.remaining()) {
        r.fail();
        return 0;
    }
    return n;
}

inline void put_count(ByteWriter& w, size_t n) { w.u32(static_cast<uint32_t>(n)); }

template <class T>
void put_vec(ByteWriter& w, const std::vector<T>& v) {
    put_count(w, v.size());
    for (const T& x : v) put<T>(w, x);
}

template <class T>
bool get_vec(ByteReader& r, std::vector<T>& out) {
    const uint32_t n = count(r, min_size<T>());
    std::vector<T> v;
    v.reserve(n);
    for (uint32_t i = 0; i < n && r.ok(); ++i) v.push_back(get<T>(r));
    if (!r.ok()) return false;
    out = std::move(v);
    return true;
}

template <class T>
void put_set(ByteWriter& w, const std::set<T>& s) {
    put_count(w, s.size());
    for (const T& x : s) put<T>(w, x);
}

// Strictly ascending keys (the canonical std::set order); anything else is invalid data.
template <class T>
bool get_set(ByteReader& r, std::set<T>& out) {
    const uint32_t n = count(r, min_size<T>());
    std::set<T> s;
    for (uint32_t i = 0; i < n && r.ok(); ++i) {
        T x = get<T>(r);
        if (!s.empty() && !(*s.rbegin() < x)) r.fail();
        s.insert(s.end(), std::move(x));
    }
    if (!r.ok()) return false;
    out = std::move(s);
    return true;
}

// Bit count, then the 64-bit words; bits past the size must be clear.
inline void put_bitset(ByteWriter& w, const galactic::Bitset& b) {
    put_count(w, b.size());
    for (const uint64_t word : b.words()) w.u64(word);
}

inline bool get_bitset(ByteReader& r, galactic::Bitset& out) {
    const uint32_t bits = r.u32();
    if (!r.ok()) return false;
    const uint64_t words = (static_cast<uint64_t>(bits) + 63u) / 64u;
    if (words * 8u > r.remaining()) {
        r.fail();
        return false;
    }
    galactic::Bitset b(bits);
    for (uint64_t k = 0; k < words; ++k) {
        const uint64_t word = r.u64();
        const uint64_t base = k * 64u;
        const uint64_t valid = bits - base >= 64u ? 64u : bits - base;
        if (valid < 64u && (word >> valid) != 0u) r.fail();
        for (uint64_t i = 0; i < valid; ++i)
            if ((word >> i) & 1u) b.set(static_cast<size_t>(base + i));
    }
    if (!r.ok()) return false;
    out = std::move(b);
    return true;
}

// Rejects the data (r.fail()) and returns false: semantic checks of the readers.
inline bool bad(ByteReader& r) {
    r.fail();
    return false;
}

// An object handle: its raw value and the object's id (kNoObjectId when the handle was stale or null).
inline void put_handle(ByteWriter& w, const objects::ObjectStore& st, objects::ObjectHandle h) {
    w.u64(h.value);
    w.i32(st.exists(h) ? st.id(h) : objects::kNoObjectId);
}

// The handle of the saved id in the restored store; a handle that was stale (or null) when saved stays stale (a slot
// the restored store reuses differently becomes null).
inline objects::ObjectHandle get_handle(ByteReader& r, const objects::ObjectStore& st) {
    const objects::ObjectHandle raw{r.u64()};
    const objects::ObjectId id = r.i32();
    if (!r.ok()) return {};
    if (id != objects::kNoObjectId) {
        const objects::ObjectHandle h = st.find(id);
        if (!h) r.fail();  // the restored store does not know the object
        return h;
    }
    return st.exists(raw) ? objects::ObjectHandle{} : raw;
}
inline constexpr size_t kHandleBytes = 8 + 4;

// ---------------------------------------------------------------- one field list per record
// A record lists its fields once, in a visit(Ar&, R&) function (R = the record, const for the writer and the hasher).
// Three archives walk that list: SaveWriterOf (the N1 save bytes), SaveReaderOf (decodes them) and RecordHasherOf (the
// per-tick CRC). Each field names the streams it belongs to: the CRC covers the fields the simulation's outcome reads, so
// a field such as a display name or a cached position is saved but not hashed. Hash-only fields (In::Hash) are markers
// and blocks the save keeps elsewhere, or a field the CRC reads at another place than the save. The CRC byte order of
// every record is the one the goldens were written with. Shared by the tactical AI (tactical_ai_records.hpp, REF1 t03)
// and the galactic AI (galactic_ai_records.hpp, #145).
enum class In : uint8_t { Both, Save, Hash };

template <class R, class T>
concept Of = std::same_as<std::remove_const_t<R>, T>;

// Each archive is a base for a concrete class (Self) declared in the records' namespace: list() passes the concrete
// archive to its element visitor, so a nested visit(ar, element) finds the field lists there.
template <class Self>
class SaveWriterOf {
public:
    explicit SaveWriterOf(ByteWriter& w, const objects::ObjectStore* st = nullptr) : w_(w), st_(st) {}
    template <class F>
    static bool since(F) {
        return true;
    }
    template <class T>
    void field(const T& v, In in = In::Both) {
        if (in != In::Hash) put<T>(w_, v);
    }
    template <class U, class T>
    void field_as(const T& v, In in = In::Both) {  // a wider in-memory value saved as U
        if (in != In::Hash) put<U>(w_, static_cast<U>(v));
    }
    template <class E>
    void enumeration(const E& e, E /*max*/, In in = In::Both) {
        if (in != In::Hash) w_.u8(static_cast<uint8_t>(e));
    }
    void str(const std::string& s, In in = In::Both) {
        if (in != In::Hash) w_.str(s);
    }
    template <class V>  // x, y, z floats
    void vec3(const V& v, In in = In::Both) {
        if (in == In::Hash) return;
        w_.f32(v.x);
        w_.f32(v.y);
        w_.f32(v.z);
    }
    template <class X>  // min_x, min_y, max_x, max_y floats
    void rect(const X& x, In in = In::Both) {
        if (in == In::Hash) return;
        w_.f32(x.min_x);
        w_.f32(x.min_y);
        w_.f32(x.max_x);
        w_.f32(x.max_y);
    }
    template <class T>
    void vec(const std::vector<T>& v, In in = In::Both) {
        if (in != In::Hash) put_vec(w_, v);
    }
    void handle(objects::ObjectHandle h, In in = In::Both) {
        if (in != In::Hash) put_handle(w_, *st_, h);
    }
    // A count, then each element through fn(archive, element); min_bytes is the reader's lower bound per element.
    template <class T, class F>
    void list(const std::vector<T>& v, In in, size_t /*min_bytes*/, F&& fn) {
        if (in == In::Hash) return;
        put_count(w_, v.size());
        for (const T& x : v) fn(static_cast<Self&>(*this), x);
    }
    void hash_tag(uint8_t) {}
    template <class T, class U>
    void absent(const T&, const U&) {}
    // A field the record no longer has, kept in the save so its byte layout stays: `value` is written, the reader drops it.
    template <class T>
    void former(const T& value) {
        put<T>(w_, value);
    }

private:
    ByteWriter& w_;
    const objects::ObjectStore* st_;
};

// The save-format features an older image lacks: Features::has(feature) (the writer and the hasher always have them).
template <class Self, class Features>
class SaveReaderOf {
public:
    explicit SaveReaderOf(ByteReader& r, const objects::ObjectStore* st = nullptr, Features f = {}) : r_(r), st_(st), f_(f) {}
    template <class F>
    bool since(F x) const {
        return f_.has(x);
    }
    bool ok() const { return r_.ok(); }
    template <class T>
    void field(T& v, In in = In::Both) {
        if (in != In::Hash) v = get<T>(r_);
    }
    template <class U, class T>
    void field_as(T& v, In in = In::Both) {
        if (in != In::Hash) v = static_cast<T>(get<U>(r_));
    }
    template <class E>
    void enumeration(E& e, E max, In in = In::Both) {  // a value past `max` is invalid data
        if (in == In::Hash) return;
        const uint8_t b = r_.u8();
        if (b > static_cast<uint8_t>(max)) r_.fail();
        e = static_cast<E>(b);
    }
    void str(std::string& s, In in = In::Both) {
        if (in != In::Hash) s = r_.str();
    }
    template <class V>
    void vec3(V& v, In in = In::Both) {
        if (in == In::Hash) return;
        v.x = r_.f32();
        v.y = r_.f32();
        v.z = r_.f32();
    }
    template <class X>
    void rect(X& x, In in = In::Both) {
        if (in == In::Hash) return;
        x.min_x = r_.f32();
        x.min_y = r_.f32();
        x.max_x = r_.f32();
        x.max_y = r_.f32();
    }
    template <class T>
    void vec(std::vector<T>& v, In in = In::Both) {
        if (in != In::Hash) get_vec(r_, v);
    }
    void handle(objects::ObjectHandle& h, In in = In::Both) {
        if (in != In::Hash) h = get_handle(r_, *st_);
    }
    template <class T, class F>
    void list(std::vector<T>& v, In in, size_t min_bytes, F&& fn) {
        if (in == In::Hash) return;
        v.clear();
        v.resize(count(r_, min_bytes));
        for (T& x : v) {
            if (!r_.ok()) break;
            fn(static_cast<Self&>(*this), x);
        }
    }
    void hash_tag(uint8_t) {}
    template <class T, class U>
    void absent(T& x, const U& value) {  // a field this image's version does not have
        x = static_cast<T>(value);
    }
    template <class T>
    void former(const T&) {
        (void)get<T>(r_);
    }

private:
    ByteReader& r_;
    const objects::ObjectStore* st_;
    Features f_;
};

template <class Self>
class RecordHasherOf {
public:
    explicit RecordHasherOf(StateHasher& h, const objects::ObjectStore* st = nullptr) : h_(h), st_(st) {}
    template <class F>
    static bool since(F) {
        return true;
    }
    template <class T>
    void field(const T& v, In in = In::Both) {
        if (in != In::Save) h_.value(v);
    }
    template <class U, class T>
    void field_as(const T& v, In in = In::Both) {
        if (in != In::Save) h_.value(static_cast<U>(v));
    }
    template <class E>
    void enumeration(const E& e, E /*max*/, In in = In::Both) {
        if (in != In::Save) h_.u8(static_cast<uint8_t>(e));
    }
    void str(const std::string& s, In in = In::Both) {
        if (in != In::Save) h_.str(s);
    }
    template <class V>
    void vec3(const V& v, In in = In::Both) {
        if (in == In::Save) return;
        h_.f32(v.x);
        h_.f32(v.y);
        h_.f32(v.z);
    }
    template <class X>
    void rect(const X& x, In in = In::Both) {
        if (in == In::Save) return;
        h_.f32(x.min_x);
        h_.f32(x.min_y);
        h_.f32(x.max_x);
        h_.f32(x.max_y);
    }
    template <class T>
    void vec(const std::vector<T>& v, In in = In::Both) {
        if (in == In::Save) return;
        h_.u32(static_cast<uint32_t>(v.size()));
        for (const T& x : v) h_.value(x);
    }
    void handle(objects::ObjectHandle h, In in = In::Both) {  // the object's id
        if (in != In::Save) h_.i32(st_->id(h));
    }
    template <class T, class F>
    void list(const std::vector<T>& v, In in, size_t /*min_bytes*/, F&& fn) {
        if (in == In::Save) return;
        h_.u32(static_cast<uint32_t>(v.size()));
        for (const T& x : v) fn(static_cast<Self&>(*this), x);
    }
    void hash_tag(uint8_t t) { h_.u8(t); }
    template <class T, class U>
    void absent(const T&, const U&) {}
    template <class T>
    void former(const T&) {}

private:
    StateHasher& h_;
    const objects::ObjectStore* st_;
};

// The element visitor of list(): the element's own visit().
inline constexpr auto visit_each = [](auto& ar, auto& x) { visit(ar, x); };
// A list of object handles (each one handle()).
inline constexpr auto handle_each = [](auto& ar, auto& h) { ar.handle(h); };
// Pair elements (i32, i32), both fields in `in`.
constexpr auto pair_fields(In in) {
    return [in](auto& ar, auto& p) {
        ar.field(p.first, in);
        ar.field(p.second, in);
    };
}

}  // namespace alamo::ai::save
