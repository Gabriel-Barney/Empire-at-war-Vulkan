// #145: the galactic AI world's state records and their one field list each, walked by the save writer, the save reader
// and the CRC hasher (the archives of ai_save_io.hpp "one field list per record", shared with the tactical AI's
// tactical_ai_records.hpp). GalacticAiWorld::write / read (galactic_ai_save.cpp) and GalacticAiWorld::hash
// (galactic_ai.cpp) walk the world's maps and vectors and visit each record; a map entry is its key (the record's id),
// written and hashed by the world, then the record. Not installed.
//
// The save bytes are those of save version 11, the CRC bytes those GalacticAiWorld::hash fed before #145: where the CRC
// reads a field at another place than the save, the field is listed twice (In::Hash where the CRC reads it, In::Save
// where the save keeps it). Records the CRC never reads (PendingBonus, SourceList, PotentialPlanRec, PpSelection) are
// saved only. Older images (save versions 6-10) lack the fields of a Feature; the reader leaves those at the record's
// defaults.
#pragma once
#include <cstddef>
#include <cstdint>

#include "ai_save_io.hpp"
#include "galactic_ai_impl.hpp"

namespace alamo::ai::galactic_io {

using save::handle_each;
using save::In;
using save::Of;
using save::visit_each;

// The galactic AI save version that added each field (GalacticAiWorld::read accepts 6 to kWorldVersion).
enum class Feature : uint32_t {
    TfSync = 7,        // TaskForceRec sync gather state (q2f t09)
    Fleet = 8,         // TaskForceRec / StatusRec FleetObject (a2m t02)
    BuildClaimed = 9,  // BuildTask::claimed
    BonusStatus = 10,  // PendingBonus::status
};
struct Features {
    uint32_t version = UINT32_MAX;  // the image's save version (the current one has every feature)
    bool has(Feature f) const { return version >= static_cast<uint32_t>(f); }
};

// The archives, declared in this namespace so a visit(archive, record) call finds the field lists below.
class SaveWriter : public save::SaveWriterOf<SaveWriter> {
public:
    using SaveWriterOf::SaveWriterOf;
};
class SaveReader : public save::SaveReaderOf<SaveReader, Features> {
public:
    using SaveReaderOf::SaveReaderOf;
};
class RecordHasher : public save::RecordHasherOf<RecordHasher> {
public:
    using RecordHasherOf::RecordHasherOf;
};

// ---------------------------------------------------------------- per AI player (saved only; PlayerAi's CRC is its context)

// PendingBonus: u32 goal type, i32 target, f32 bonus, i64 deadline, i32 blocking status (version 10).
inline constexpr size_t kBonusBytes = 4 + 4 + 4 + 8;  // version 6-9

template <class Ar, Of<PendingBonus> R>
void visit(Ar& ar, R& b) {
    ar.field(b.goal_type, In::Save);
    ar.field(b.target, In::Save);
    ar.field(b.bonus, In::Save);
    ar.field(b.deadline, In::Save);
    if (ar.since(Feature::BonusStatus)) ar.field(b.status, In::Save);  // older images: no blocking status
}

// PlayerAi::SourceList: i64 frame, the planets.
template <class Ar, Of<PlayerAi::SourceList> R>
void visit(Ar& ar, R& s) {
    ar.field(s.frame, In::Save);
    ar.vec(s.planets, In::Save);
}

// ---------------------------------------------------------------- plans, task forces, statuses

// PlanInstance after its id; the plan's VM (a bool, then its image) follows in GalacticAiWorld::write / read. The CRC
// reads player, plan, goal type, target, result, the removable flag and the live thread count.
inline constexpr size_t kPlanBytes = 50;  // with its id and the VM flag

template <class Ar, Of<PlanInstance> R>
void visit(Ar& ar, R& p) {
    ar.field(p.player);
    ar.field(p.plan);
    ar.field(p.goal_type);
    ar.field(p.goal_key, In::Save);
    ar.field(p.category, In::Save);
    ar.field(p.target);
    ar.field(p.target_owner, In::Save);
    ar.field(p.pp, In::Save);
    ar.vec(p.tfs, In::Save);
    ar.field(p.result);
    ar.field(p.non_removable);
    ar.field(p.test_contrast, In::Save);
    ar.field(p.blocking, In::Save);
    ar.field(p.ended, In::Save);
    ar.field(p.live_threads);  // cached on the sim thread: hashers may run on job workers (no VM access)
}

// TaskForceRec after its id. The CRC reads stage, area, the FleetObject (TF +0x120), a sync gather's countdown (only
// when `sync`: other task forces hash as before q2f t09) and last the members' ids; the save keeps the fleet after the
// sync state and the members after the name.
inline constexpr size_t kTaskForceBytes = 40;  // with its id

template <class Ar, Of<TaskForceRec> R>
void visit(Ar& ar, R& t) {
    ar.field(t.plan, In::Save);
    ar.field(t.player, In::Save);
    ar.field(t.def, In::Save);
    ar.str(t.name, In::Save);
    ar.list(t.units, In::Save, save::kHandleBytes, handle_each);
    ar.vec(t.wanted, In::Save);
    ar.vec(t.wanted_source, In::Save);
    ar.vec(t.builds, In::Save);
    ar.field(t.stage);
    ar.field(t.area);
    ar.field(t.fleet, In::Hash);
    if (ar.since(Feature::TfSync)) {
        ar.field(t.sync, In::Save);
        const In gather = t.sync ? In::Both : In::Save;  // `sync` gates the countdown's CRC
        ar.field(t.sync_started, gather);
        ar.field(t.sync_left, gather);
        ar.field(t.sync_frame, In::Save);
        ar.list(t.sync_units, In::Save, save::kHandleBytes, handle_each);
        ar.vec(t.sync_etas, In::Save);
    }
    if (ar.since(Feature::Fleet)) ar.field(t.fleet, In::Save);  // checked against the fleets where it is read (tf_fleet_object)
    ar.field(t.had_units, In::Save);
    ar.field(t.no_units_sent, In::Save);
    ar.field(t.alive, In::Save);
    ar.list(t.units, In::Hash, save::kHandleBytes, handle_each);
}

// StatusRec after its id. The CRC reads finished and result; a MoveTo status also where it goes, how and which fleet
// it moves (#119), the other kinds hash as before.
inline constexpr size_t kStatusBytes = 36;  // with its id

template <class Ar, Of<StatusRec> R>
void visit(Ar& ar, R& s) {
    ar.field(s.plan, In::Save);
    ar.field(s.tf, In::Save);
    ar.enumeration(s.kind, StatusKind::DesireBonus, In::Save);
    ar.field(s.finished);
    ar.enumeration(s.result, lua::AiStatusResult::False);
    const In move = s.kind == StatusKind::MoveTo ? In::Both : In::Save;  // `kind` gates the destination's CRC
    ar.field(s.dest, move);
    ar.field(s.mode, move);
    if (ar.since(Feature::Fleet)) ar.field(s.fleet, move);  // checked against the fleets where it is read (the MoveTo step)
    ar.field(s.started, In::Save);
    ar.enumeration(s.op, lua::AiBudgetOp::WaitUnallocated, In::Save);
    ar.field(s.amount, In::Save);
}

// ---------------------------------------------------------------- PotentialPlans (saved only)

// PpSelection: u32 type, the unit, i32 task force, u32 source planet, u8 queue, f32 build time.
inline constexpr size_t kSelectionBytes = 4 + save::kHandleBytes + 4 + 4 + 1 + 4;

template <class Ar, Of<PpSelection> R>
void visit(Ar& ar, R& s) {
    ar.field(s.type, In::Save);
    ar.handle(s.unit, In::Save);
    ar.field(s.tf, In::Save);
    ar.field(s.source, In::Save);
    ar.field(s.queue, In::Save);
    ar.field(s.btime, In::Save);
}

// PotentialPlanRec after its id (the map key).
inline constexpr size_t kPotentialPlanBytes = 48;  // with its id

template <class Ar, Of<PotentialPlanRec> R>
void visit(Ar& ar, R& pp) {
    ar.field(pp.player, In::Save);
    ar.field(pp.plan, In::Save);
    ar.field(pp.goal_type, In::Save);
    ar.field(pp.target, In::Save);
    ar.list(pp.sel, In::Save, kSelectionBytes, visit_each);
    ar.field(pp.gross, In::Save);
    ar.field(pp.net, In::Save);
    ar.field(pp.eta, In::Save);
    ar.field(pp.contrast_ok, In::Save);
    ar.field(pp.queues_reserved, In::Save);
    ar.field(pp.pop, In::Save);
    ar.vec(pp.reserved_units, In::Save);
}

// ---------------------------------------------------------------- build tasks and planet queues

// BuildTask after its id. The CRC reads type, task force, state, bid planet, bid and queue id; it reads the type before
// the task force, the save after it.
inline constexpr size_t kBuildBytesV8 = 71;  // with its id
inline constexpr size_t kBuildBytes = kBuildBytesV8 + 8;  // version 9: the claim frame

template <class Ar, Of<BuildTask> R>
void visit(Ar& ar, R& b) {
    ar.field(b.plan, In::Save);
    ar.field(b.type, In::Hash);
    ar.field(b.tf);
    ar.field(b.player, In::Save);
    ar.field(b.type, In::Save);
    ar.field(b.dest, In::Save);
    ar.field(b.queue, In::Save);
    ar.enumeration(b.state, BuildState::Failed);
    ar.field(b.bound, In::Save);
    ar.field(b.bid_planet);
    ar.field(b.bid);
    ar.field(b.created, In::Save);
    ar.field(b.next_poll, In::Save);
    ar.field(b.charged, In::Save);
    // The former BuildTask::charge (always 0; dropped in a2m t05): the float is kept so the byte layout stays the same
    // across versions 6-8 (and every later version). Read and dropped.
    ar.former(0.0f);
    ar.field(b.queue_id);
    ar.field(b.producing_at, In::Save);
    ar.field(b.queue_entries_before, In::Save);
    ar.field(b.goal_key, In::Save);
    ar.field(b.category, In::Save);
    ar.field(b.listed, In::Save);
    ar.field(b.reclaimed, In::Save);
    if (ar.since(Feature::BuildClaimed)) ar.field(b.claimed, In::Save);  // older images: no timer running
}

// QueueReservation: i32 PotentialPlan, u32 type (both hashed), f32 time. The `suspended` flag is neither saved nor
// hashed (a live slot's is 0 between commits, an empty slot's is never read).
inline constexpr size_t kReservationBytes = 4 + 4 + 4;

template <class Ar, Of<QueueReservation> R>
void visit(Ar& ar, R& q) {
    ar.field(q.pp);
    ar.field(q.type);
    ar.field(q.time, In::Save);
}

// PlanetBuildQueue: the CRC reads the claimed tasks after the producing task, the save before it; the current type is
// saved only. #196: the reservations by position, empty slots included.
inline constexpr size_t kPlanetQueueBytes = 8 + 4 + 4 + 4 + 4 + 4;  // 28

template <class Ar, Of<PlanetBuildQueue> R>
void visit(Ar& ar, R& q) {
    ar.field(q.last_run);
    ar.vec(q.tasks, In::Save);
    ar.field(q.current);
    ar.field(q.current_queue_id);
    ar.field(q.current_type, In::Save);
    ar.vec(q.tasks, In::Hash);
    ar.list(q.reservations, In::Both, kReservationBytes, visit_each);
}

// FreeStoreMove: the unit, i32 player, u32 destination, the done flag. The CRC reads the unit's id and the destination.
inline constexpr size_t kFreeStoreMoveBytes = save::kHandleBytes + 4 + 4 + 1;

template <class Ar, Of<FreeStoreMove> R>
void visit(Ar& ar, R& m) {
    ar.handle(m.unit);
    ar.field(m.player, In::Save);
    ar.field(m.dest);
    ar.field(m.done, In::Save);
}

// A record's CRC bytes (the world's hash feeds the map key first); `st` resolves object handles.
template <class R>
void hash_record(StateHasher& h, const R& x, const objects::ObjectStore* st = nullptr) {
    RecordHasher a(h, st);
    visit(a, x);
}

}  // namespace alamo::ai::galactic_io
