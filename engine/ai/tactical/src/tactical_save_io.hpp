// N1 save / load helpers of the tactical AI (tactical_save.cpp, tactical_ai.cpp). Not installed. Byte rules of
// alamo/core/save_file.hpp: little-endian fields, counts before data, floats as bit patterns, no pointers / size_t.
#pragma once
#include <bit>
#include <concepts>
#include <cstdint>
#include <string>
#include <type_traits>
#include <vector>

#include "ai_save_io.hpp"  // engine/ai/src, through alamo_ai_save_io
#include "alamo/ai/tactical/hint_zones.hpp"
#include "alamo/ai/tactical/tactical_pp.hpp"
#include "alamo/ai/tactical/target_list.hpp"
#include "alamo/core/bytes.hpp"
#include "alamo/core/sim_crc.hpp"
#include "alamo/objects/object_store.hpp"

namespace alamo::ai::tactical::save_io {

using ai::save::bad;

// The u32 version that starts every record; false (r.fail()) on another value or truncation.
inline bool version(ByteReader& r, uint32_t want) {
    const uint32_t v = r.u32();
    return (r.ok() && v == want) || bad(r);
}

// The shared AI save codec (count, put_count, put / get, min_size, put_vec / get_vec, the object handles) and the record
// archives below: engine/ai/src/ai_save_io.hpp, exported to this library by the alamo_ai_save_io INTERFACE target. Same
// byte layout.
using ai::save::count;
using ai::save::get;
using ai::save::get_handle;
using ai::save::get_vec;
using ai::save::min_size;
using ai::save::put;
using ai::save::put_count;
using ai::save::put_handle;
using ai::save::put_vec;

inline void put_rect(ByteWriter& w, const Rect2& x) {
    w.f32(x.min_x);
    w.f32(x.min_y);
    w.f32(x.max_x);
    w.f32(x.max_y);
}

inline Rect2 get_rect(ByteReader& r) {
    Rect2 x;
    x.min_x = r.f32();
    x.min_y = r.f32();
    x.max_x = r.f32();
    x.max_y = r.f32();
    return x;
}

inline void put_vec3(ByteWriter& w, const Vec3f& v) {
    w.f32(v.x);
    w.f32(v.y);
    w.f32(v.z);
}

inline Vec3f get_vec3(ByteReader& r) {
    Vec3f v;
    v.x = r.f32();
    v.y = r.f32();
    v.z = r.f32();
    return v;
}

// ---------------------------------------------------------------- one field list per record
// The records' visit(Ar&, R&) field lists (here and in tactical_ai_records.hpp) and the archives that walk them:
// ai_save_io.hpp "one field list per record" (#145 moved them there for the galactic AI). The CRC covers the fields the
// battle's outcome reads.
using ai::save::handle_each;
using ai::save::In;
using ai::save::Of;
using ai::save::pair_fields;
using ai::save::visit_each;

// Save-format features an older image lacks (SaveReader); the writer and the hasher always have them.
enum class Feature : uint8_t { PadFact, Retest, Escrow };
struct Features {
    bool pad_fact = true;  // TacticalAiObject::pad_occupied (tactical AI save version 14 / 15)
    bool retest = true;    // pick contrast entries, PP layer flags and re-test byte (version 16 / 17)
    bool escrow = true;    // b0147: PP net cost, escrow and TacticalBuild picks; a pending solve's net (version 20 / 21)
    bool has(Feature x) const { return x == Feature::PadFact ? pad_fact : x == Feature::Retest ? retest : escrow; }
};

// The shared archives, declared in this namespace so a visit(archive, record) call finds the records' field lists here.
class SaveWriter : public ai::save::SaveWriterOf<SaveWriter> {
public:
    using SaveWriterOf::SaveWriterOf;
};
class SaveReader : public ai::save::SaveReaderOf<SaveReader, Features> {
public:
    using SaveReaderOf::SaveReaderOf;
};
class RecordHasher : public ai::save::RecordHasherOf<RecordHasher> {
public:
    using RecordHasherOf::RecordHasherOf;
};

// TacticalTarget (target lists, AI target slots): u8 kind, i32 object id, name, position, evaluator rect, zone, cell x / y.
// The CRC of an AI target slot reads kind, object id, zone and cell.
inline constexpr size_t kTargetBytes = 1 + 4 + 4 + 12 + 16 + 4 + 4 + 4;

template <class Ar, Of<TacticalTarget> R>
void visit(Ar& ar, R& t) {
    ar.enumeration(t.kind, TargetKind::Region);
    ar.field(t.object_id);
    ar.str(t.name, In::Save);
    ar.vec3(t.position, In::Save);
    ar.rect(t.eval_rect, In::Save);
    ar.field(t.zone);
    ar.field(t.cell_x);
    ar.field(t.cell_y);
}

inline void put_target(ByteWriter& w, const TacticalTarget& t) {
    SaveWriter a(w);
    visit(a, t);
}

inline bool get_target(ByteReader& r, TacticalTarget& t) {
    SaveReader a(r);
    visit(a, t);
    return r.ok();
}

// TacticalPpPick (PotentialPlan picks): i32 type, u8 source, i32 ref, i32 task force, i32 team, i32 contrast entry (skai
// t15, tactical AI save version 16 / 17; -1 in older images). The CRC reads all but the contrast entry.
inline constexpr size_t kPickBytes = 4 + 1 + 4 + 4 + 4;  // lower bound (skai t15 adds the contrast entry, 4)

template <class Ar, Of<TacticalPpPick> R>
void visit(Ar& ar, R& k) {
    ar.field(k.type);
    ar.enumeration(k.source, PpSource::Produce);
    ar.field(k.ref);
    ar.field(k.tf);
    ar.field(k.team);
    if (ar.since(Feature::Retest))
        ar.field(k.contrast_entry, In::Save);
    else
        ar.absent(k.contrast_entry, -1);
}

// Bit-exact float equality (geometry checks: a reader built from the same data has the same bits).
inline bool same_bits(float a, float b) { return std::bit_cast<uint32_t>(a) == std::bit_cast<uint32_t>(b); }

}  // namespace alamo::ai::tactical::save_io
