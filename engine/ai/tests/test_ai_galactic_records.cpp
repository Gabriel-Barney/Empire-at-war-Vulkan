// alamo_ai_galactic_records_tests: #145 - the galactic AI world's state records and their one field list each
// (src/galactic_ai_records.hpp), walked by the save writer, the save reader and the CRC hasher:
//   * round trip: write -> read -> write gives the same bytes and the same CRC for every record;
//   * save pin: each record's save bytes are the ones GalacticAiWorld::write put before #145 (save version 11);
//   * CRC pin: each hashed record's CRC bytes are the ones GalacticAiWorld::hash fed before #145 (the goldens' order);
//   * older images: the save versions 6-10 layouts read with the fields they lack at the record's defaults;
//   * field coverage: changing any one field changes the save bytes (a saved field) and the CRC (a hashed field), a
//     saved-only field leaves the CRC alone (unless it gates hashed fields: TaskForceRec::sync, StatusRec::kind), and
//     the changed record still round-trips. A member listed twice (the CRC reads it at another place than the save)
//     counts as both.
#include <cstdio>
#include <map>
#include <set>
#include <span>
#include <string>
#include <type_traits>
#include <vector>

#include "alamo/core/bytes.hpp"
#include "alamo/core/sim_crc.hpp"
#include "alamo/lua/lua_vm.hpp"  // PlanInstance owns its VM
#include "alamo/objects/game_object_types.hpp"
#include "alamo/objects/object_store.hpp"
#include "alamo/test/support.hpp"
#include "galactic_ai_records.hpp"  // engine/ai/src (not installed)

using namespace alamo;
using namespace alamo::ai;
namespace io = alamo::ai::galactic_io;

using ::alamo::test::g_fail;

namespace {

// Two live objects: record handles point at the first, the coverage walk swaps in the second.
struct Store {
    objects::GameObjectTypes types;
    objects::IdCounter ids;
    objects::ObjectStore store{objects::ModeKind::Galactic, objects::StoreKind::Primary, types, &ids};
    objects::ObjectHandle a, b;
    Store() {
        objects::SpawnParams sp;
        sp.owner = 1;
        a = store.create(sp);
        b = store.create(sp);
    }
};

// ---------------------------------------------------------------- samples: a non-default value in every field

PendingBonus sample_bonus(const Store&) {
    PendingBonus b;
    b.goal_type = 3;
    b.target = 4;
    b.bonus = 1.5f;
    b.deadline = 900;
    b.status = 7;
    return b;
}

PlayerAi::SourceList sample_sources(const Store&) {
    PlayerAi::SourceList s;
    s.frame = 33;
    s.planets = {1, 2};
    return s;
}

PlanInstance sample_plan(const Store&) {
    PlanInstance p;
    p.id = 21;
    p.player = 1;
    p.plan = 2;
    p.goal_type = 3;
    p.goal_key = 4;
    p.category = 5;
    p.target = 6;
    p.target_owner = 7;
    p.pp = 8;
    p.tfs = {9, 10};
    p.result = true;
    p.non_removable = true;
    p.test_contrast = false;
    p.blocking = true;
    p.ended = true;
    p.live_threads = 3;
    return p;
}

TaskForceRec sample_tf(const Store& s) {
    TaskForceRec t;
    t.id = 22;
    t.plan = 2;
    t.player = 1;
    t.def = 3;
    t.name = "TF_1";
    t.units = {s.a};
    t.wanted = {4};
    t.wanted_source = {5};
    t.builds = {6};
    t.stage = 7;
    t.area = 8;
    t.fleet = 9;
    t.sync = true;
    t.sync_started = true;
    t.sync_left = 2.5;
    t.sync_frame = 77;
    t.sync_units = {s.a};
    t.sync_etas = {1.25};
    t.had_units = true;
    t.no_units_sent = true;
    t.alive = false;
    return t;
}

StatusRec sample_status(const Store&) {
    StatusRec s;
    s.id = 23;
    s.plan = 2;
    s.tf = 3;
    s.kind = StatusKind::MoveTo;
    s.finished = true;
    s.result = lua::AiStatusResult::True;
    s.dest = 4;
    s.mode = 1;
    s.fleet = 6;
    s.started = 50;
    s.op = lua::AiBudgetOp::FlushAll;
    s.amount = 12.5;
    return s;
}

PpSelection sample_selection(const Store& s) {
    PpSelection x;
    x.type = 3;
    x.unit = s.a;
    x.tf = 2;
    x.source = 4;
    x.queue = 0;
    x.btime = 1.5f;
    return x;
}

PotentialPlanRec sample_pp(const Store& s) {
    PotentialPlanRec pp;
    pp.player = 1;
    pp.plan = 2;
    pp.goal_type = 3;
    pp.target = 4;
    pp.sel = {sample_selection(s)};
    pp.gross = 10.0f;
    pp.net = 9.0f;
    pp.eta = 8.0f;
    pp.contrast_ok = false;
    pp.queues_reserved = true;
    pp.pop = 2.0;
    pp.reserved_units = {41};
    return pp;
}

BuildTask sample_build(const Store&) {
    BuildTask b;
    b.id = 24;
    b.plan = 2;
    b.tf = 3;
    b.player = 1;
    b.type = 4;
    b.dest = 5;
    b.queue = 0;
    b.state = BuildState::Producing;
    b.bound = true;
    b.bid_planet = 6;
    b.bid = 7.5;
    b.created = 8;
    b.next_poll = 9;
    b.charged = true;
    b.queue_id = 10;
    b.producing_at = 11;
    b.queue_entries_before = 12;
    b.goal_key = 13;
    b.category = 14;
    b.listed = true;
    b.reclaimed = true;
    b.claimed = 15;
    return b;
}

QueueReservation sample_reservation(const Store&) {
    QueueReservation q;
    q.pp = 6;
    q.type = 7;
    q.time = 1.5f;
    return q;
}

PlanetBuildQueue sample_queue(const Store& s) {
    PlanetBuildQueue q;
    q.last_run = 20;
    q.tasks = {1, 2};
    q.current = 3;
    q.current_queue_id = 4;
    q.current_type = 5;
    q.reservations = {sample_reservation(s), QueueReservation{}};  // #196: an empty slot by position
    return q;
}

FreeStoreMove sample_move(const Store& s) {
    FreeStoreMove m;
    m.unit = s.a;
    m.player = 1;
    m.dest = 2;
    m.done = true;
    return m;
}

// ---------------------------------------------------------------- the three archives

template <class R>
std::vector<uint8_t> save_bytes(const R& x, const Store& s) {
    ByteWriter w;
    io::SaveWriter a(w, &s.store);
    visit(a, x);
    return w.bytes();
}

template <class R>
uint64_t crc(const R& x, const Store& s) {
    StateHasher h;
    io::RecordHasher a(h, &s.store);
    visit(a, x);
    return h.digest();
}

template <class R>
bool load(std::span<const uint8_t> bytes, const Store& s, R& out, uint32_t version = UINT32_MAX) {
    ByteReader r(bytes);
    io::SaveReader a(r, &s.store, io::Features{version});
    visit(a, out);
    return r.ok() && r.done();
}

// write -> read -> write: same bytes, same CRC.
template <class R, class Make>
bool round_trip(const char* name, Make make, const Store& s) {
    const R x = make(s);
    const std::vector<uint8_t> bytes = save_bytes(x, s);
    R y{};
    const bool loaded = load(bytes, s, y);
    const bool ok = loaded && save_bytes(y, s) == bytes && crc(y, s) == crc(x, s);
    if (!ok) std::printf("  round trip FAILED: %s\n", name);
    return ok;
}

}  // namespace

// ---------------------------------------------------------------- field coverage walk

namespace alamo::ai::galactic_io {

// Changes the k-th field the visit lists (an archive like the three above) and reports which streams it is in: its
// own In, narrowed by the list holding it (a field of a saved-only list is saved only), widened by the other places the
// list names the same member (a member the CRC reads at another place than the save). Declared in galactic_io, like
// the other archives, so visit() calls on it find the records' field lists.
class ChangeField {
public:
    ChangeField(size_t k, objects::ObjectHandle other) : k_(k), other_(other) {}
    template <class F>
    static bool since(F) {
        return true;
    }
    bool hit() const { return hit_; }
    // Saved / hashed: from every place the list names the changed member.
    bool saved() const { return roles(In::Save); }
    bool hashed() const { return roles(In::Hash); }
    const void* changed() const { return at_; }

    template <class T>
    void field(T& v, In in = In::Both) {
        if (!take(&v, in)) return;
        if constexpr (std::is_same_v<T, bool>)
            v = !v;
        else if constexpr (std::is_floating_point_v<T>)
            v += T(1);
        else
            v = static_cast<T>(v + 1);
    }
    template <class U, class T>
    void field_as(T& v, In in = In::Both) {
        if (take(&v, in)) v = static_cast<T>(v + 1);
    }
    template <class E>
    void enumeration(E& e, E max, In in = In::Both) {
        if (!take(&e, in)) return;
        const auto b = static_cast<uint8_t>(e);
        e = static_cast<E>(b < static_cast<uint8_t>(max) ? b + 1 : b - 1);
    }
    void str(std::string& s, In in = In::Both) {
        if (take(&s, in)) s += "x";
    }
    template <class T>
    void vec(std::vector<T>& v, In in = In::Both) {
        if (take(&v, in)) v.push_back(T(7));
    }
    void handle(objects::ObjectHandle& h, In in = In::Both) {
        if (take(&h, in)) h = other_;
    }
    template <class T, class F>
    void list(std::vector<T>& v, In in, size_t, F&& fn) {
        if (take(&v, in)) {  // the list itself: one more element
            v.push_back(v.empty() ? T{} : v.front());
            return;
        }
        outer_.push_back(in);
        for (T& x : v) fn(*this, x);
        outer_.pop_back();
    }
    void hash_tag(uint8_t) {}
    template <class T, class U>
    void absent(T&, const U&) {}
    template <class T>
    void former(const T&) {}

private:
    bool take(const void* at, In in) {
        In role = in;
        for (const In o : outer_)
            if (o != In::Both) role = o;
        seen_[at].insert(role);
        if (n_++ != k_) return false;
        at_ = at;
        hit_ = true;
        return true;
    }
    bool roles(In stream) const {
        const auto it = seen_.find(at_);
        if (it == seen_.end()) return false;
        for (const In r : it->second)
            if (r == In::Both || r == stream) return true;
        return false;
    }
    size_t k_, n_ = 0;
    objects::ObjectHandle other_;
    std::vector<In> outer_;
    std::map<const void*, std::set<In>> seen_;
    const void* at_ = nullptr;
    bool hit_ = false;
};

}  // namespace alamo::ai::galactic_io

namespace {

using io::ChangeField;

// Walks every field of the record; `gate` names the saved-only member whose value decides which fields the CRC reads
// (nullptr: none). Returns the number of fields changed.
template <class R, class Make>
bool coverage(const char* name, Make make, const Store& s, size_t& fields, const void* (*gate)(const R&) = nullptr) {
    const R base = make(s);
    const std::vector<uint8_t> b0 = save_bytes(base, s);
    const uint64_t h0 = crc(base, s);
    bool ok = true;
    for (size_t k = 0;; ++k) {
        R x = make(s);
        ChangeField c(k, s.b);
        visit(c, x);
        if (!c.hit()) break;
        ++fields;
        const std::vector<uint8_t> b1 = save_bytes(x, s);
        const uint64_t h1 = crc(x, s);
        const bool gates = gate && gate(x) == c.changed();
        bool good = (c.saved() ? b1 != b0 : b1 == b0) && (c.hashed() ? h1 != h0 : (h1 == h0 || gates));
        R y{};
        const bool loaded = load(b1, s, y);
        good = good && loaded && save_bytes(y, s) == b1 && crc(y, s) == h1;
        if (!good)
            std::printf("  coverage FAILED: %s field %zu (saved %d, hashed %d)\n", name, k, c.saved() ? 1 : 0, c.hashed() ? 1 : 0);
        ok = ok && good;
    }
    return ok;
}

// ---------------------------------------------------------------- save pin (GalacticAiWorld::write before #145)

void old_bonus(ByteWriter& w, const PendingBonus& b) {
    w.u32(b.goal_type);
    w.i32(b.target);
    w.f32(b.bonus);
    w.i64(b.deadline);
    w.i32(b.status);  // v10
}

void old_sources(ByteWriter& w, const PlayerAi::SourceList& s) {
    w.i64(s.frame);
    save::put_vec(w, s.planets);
}

void old_plan(ByteWriter& w, const PlanInstance& pi) {
    w.i32(pi.player);
    w.u32(pi.plan);
    w.u32(pi.goal_type);
    w.u32(pi.goal_key);
    w.i32(pi.category);
    w.i32(pi.target);
    w.i32(pi.target_owner);
    w.i32(pi.pp);
    save::put_vec(w, pi.tfs);
    w.boolean(pi.result);
    w.boolean(pi.non_removable);
    w.boolean(pi.test_contrast);
    w.boolean(pi.blocking);
    w.boolean(pi.ended);
    w.u32(pi.live_threads);
}

void old_handles(ByteWriter& w, const objects::ObjectStore& st, const std::vector<objects::ObjectHandle>& v) {
    w.u32(static_cast<uint32_t>(v.size()));
    for (const objects::ObjectHandle h : v) save::put_handle(w, st, h);
}

// Version 6 (no sync gather state, no fleet) when `version` < 7, version 7 (no fleet) when < 8.
void old_tf(ByteWriter& w, const TaskForceRec& t, const objects::ObjectStore& st, uint32_t version = 11) {
    w.i32(t.plan);
    w.i32(t.player);
    w.u32(t.def);
    w.str(t.name);
    old_handles(w, st, t.units);
    save::put_vec(w, t.wanted);
    save::put_vec(w, t.wanted_source);
    save::put_vec(w, t.builds);
    w.i32(t.stage);
    w.i32(t.area);
    if (version >= 7) {
        w.boolean(t.sync);
        w.boolean(t.sync_started);
        w.f64(t.sync_left);
        w.i64(t.sync_frame);
        old_handles(w, st, t.sync_units);
        save::put_vec(w, t.sync_etas);
    }
    if (version >= 8) w.u32(t.fleet);
    w.boolean(t.had_units);
    w.boolean(t.no_units_sent);
    w.boolean(t.alive);
}

void old_status(ByteWriter& w, const StatusRec& s, uint32_t version = 11) {
    w.i32(s.plan);
    w.i32(s.tf);
    w.u8(static_cast<uint8_t>(s.kind));
    w.boolean(s.finished);
    w.u8(static_cast<uint8_t>(s.result));
    w.u32(s.dest);
    w.u8(s.mode);
    if (version >= 8) w.u32(s.fleet);
    w.i64(s.started);
    w.u8(static_cast<uint8_t>(s.op));
    w.f64(s.amount);
}

void old_pp(ByteWriter& w, const PotentialPlanRec& pp, const objects::ObjectStore& st) {
    w.i32(pp.player);
    w.u32(pp.plan);
    w.u32(pp.goal_type);
    w.i32(pp.target);
    w.u32(static_cast<uint32_t>(pp.sel.size()));
    for (const PpSelection& s : pp.sel) {
        w.u32(s.type);
        save::put_handle(w, st, s.unit);
        w.i32(s.tf);
        w.u32(s.source);
        w.u8(s.queue);
        w.f32(s.btime);
    }
    w.f32(pp.gross);
    w.f32(pp.net);
    w.f32(pp.eta);
    w.boolean(pp.contrast_ok);
    w.boolean(pp.queues_reserved);
    w.f64(pp.pop);
    save::put_vec(w, pp.reserved_units);
}

void old_build(ByteWriter& w, const BuildTask& b, uint32_t version = 11) {
    w.i32(b.plan);
    w.i32(b.tf);
    w.i32(b.player);
    w.u32(b.type);
    w.u32(b.dest);
    w.u8(b.queue);
    w.u8(static_cast<uint8_t>(b.state));
    w.boolean(b.bound);
    w.u32(b.bid_planet);
    w.f64(b.bid);
    w.i64(b.created);
    w.i64(b.next_poll);
    w.boolean(b.charged);
    w.f32(0.0f);
    w.i32(b.queue_id);
    w.u32(b.producing_at);
    w.i32(b.queue_entries_before);
    w.u32(b.goal_key);
    w.i32(b.category);
    w.boolean(b.listed);
    w.boolean(b.reclaimed);
    if (version >= 9) w.i64(b.claimed);  // v9
}

void old_queue(ByteWriter& w, const PlanetBuildQueue& q) {
    w.i64(q.last_run);
    save::put_vec(w, q.tasks);
    w.i32(q.current);
    w.i32(q.current_queue_id);
    w.u32(q.current_type);
    w.u32(static_cast<uint32_t>(q.reservations.size()));
    for (const QueueReservation& res : q.reservations) {
        w.i32(res.pp);
        w.u32(res.type);
        w.f32(res.time);
    }
}

void old_move(ByteWriter& w, const FreeStoreMove& mv, const objects::ObjectStore& st) {
    save::put_handle(w, st, mv.unit);
    w.i32(mv.player);
    w.u32(mv.dest);
    w.boolean(mv.done);
}

template <class R, class Make, class Old>
bool save_pinned(const char* name, Make make, Old old, const Store& s) {
    const R x = make(s);
    ByteWriter w;
    old(w, x);
    const bool ok = save_bytes(x, s) == w.bytes();
    if (!ok) std::printf("  save pin FAILED: %s\n", name);
    return ok;
}

// ---------------------------------------------------------------- CRC pin (GalacticAiWorld::hash before #145, after the id)

void pin_plan(StateHasher& h, const PlanInstance& p) {
    h.i32(p.player);
    h.u32(p.plan);
    h.u32(p.goal_type);
    h.i32(p.target);
    h.boolean(p.result);
    h.boolean(p.non_removable);
    h.u32(p.live_threads);
}

void pin_tf(StateHasher& h, const TaskForceRec& t, const objects::ObjectStore& st) {
    h.i32(t.stage);
    h.i32(t.area);
    h.u32(t.fleet);
    if (t.sync) {
        h.boolean(t.sync_started);
        h.f64(t.sync_left);
    }
    h.u32(static_cast<uint32_t>(t.units.size()));
    for (const objects::ObjectHandle u : t.units) h.i32(st.id(u));
}

void pin_status(StateHasher& h, const StatusRec& s) {  // hash_status (#119)
    h.boolean(s.finished);
    h.u8(static_cast<uint8_t>(s.result));
    if (s.kind == StatusKind::MoveTo) {
        h.u32(s.dest);
        h.u8(s.mode);
        h.u32(s.fleet);
    }
}

void pin_build(StateHasher& h, const BuildTask& b) {
    h.u32(b.type);
    h.i32(b.tf);
    h.u8(static_cast<uint8_t>(b.state));
    h.u32(b.bid_planet);
    h.f64(b.bid);
    h.i32(b.queue_id);
}

void pin_queue(StateHasher& h, const PlanetBuildQueue& q) {
    h.i64(q.last_run);
    h.i32(q.current);
    h.i32(q.current_queue_id);
    h.u32(static_cast<uint32_t>(q.tasks.size()));
    for (const int32_t t : q.tasks) h.i32(t);
    h.u32(static_cast<uint32_t>(q.reservations.size()));
    for (const QueueReservation& r : q.reservations) {
        h.i32(r.pp);
        h.u32(r.type);
    }
}

void pin_move(StateHasher& h, const FreeStoreMove& m, const objects::ObjectStore& st) {
    h.i32(st.id(m.unit));
    h.u32(m.dest);
}

void pin_nothing(StateHasher&, const auto&) {}  // a record the CRC never reads

template <class R, class Make, class Pin>
bool crc_pinned(const char* name, Make make, Pin pin, const Store& s) {
    const R x = make(s);
    StateHasher h;
    pin(h, x);
    const bool ok = crc(x, s) == h.digest();
    if (!ok) std::printf("  CRC pin FAILED: %s\n", name);
    return ok;
}

// ---------------------------------------------------------------- the tests

void test_round_trip(const Store& s) {
    const bool ok = round_trip<PendingBonus>("PendingBonus", sample_bonus, s) &&
                    round_trip<PlayerAi::SourceList>("SourceList", sample_sources, s) &&
                    round_trip<PlanInstance>("PlanInstance", sample_plan, s) && round_trip<TaskForceRec>("TaskForceRec", sample_tf, s) &&
                    round_trip<StatusRec>("StatusRec", sample_status, s) && round_trip<PpSelection>("PpSelection", sample_selection, s) &&
                    round_trip<PotentialPlanRec>("PotentialPlanRec", sample_pp, s) &&
                    round_trip<BuildTask>("BuildTask", sample_build, s) &&
                    round_trip<QueueReservation>("QueueReservation", sample_reservation, s) &&
                    round_trip<PlanetBuildQueue>("PlanetBuildQueue", sample_queue, s) &&
                    round_trip<FreeStoreMove>("FreeStoreMove", sample_move, s);
    CHECK(ok);
}

void test_save_pin(const Store& s) {
    const objects::ObjectStore& st = s.store;
    bool ok = save_pinned<PendingBonus>("PendingBonus", sample_bonus, old_bonus, s);
    ok = save_pinned<PlayerAi::SourceList>("SourceList", sample_sources, old_sources, s) && ok;
    ok = save_pinned<PlanInstance>("PlanInstance", sample_plan, old_plan, s) && ok;
    ok = save_pinned<TaskForceRec>("TaskForceRec", sample_tf, [&](ByteWriter& w, const TaskForceRec& t) { old_tf(w, t, st); }, s) && ok;
    ok = save_pinned<StatusRec>("StatusRec", sample_status, [](ByteWriter& w, const StatusRec& x) { old_status(w, x); }, s) && ok;
    ok = save_pinned<PotentialPlanRec>("PotentialPlanRec", sample_pp, [&](ByteWriter& w, const PotentialPlanRec& x) { old_pp(w, x, st); }, s) && ok;
    ok = save_pinned<BuildTask>("BuildTask", sample_build, [](ByteWriter& w, const BuildTask& x) { old_build(w, x); }, s) && ok;
    ok = save_pinned<PlanetBuildQueue>("PlanetBuildQueue", sample_queue, old_queue, s) && ok;
    ok = save_pinned<FreeStoreMove>("FreeStoreMove", sample_move, [&](ByteWriter& w, const FreeStoreMove& x) { old_move(w, x, st); }, s) && ok;
    CHECK(ok);
}

void test_crc_pin(const Store& s) {
    const objects::ObjectStore& st = s.store;
    bool ok = crc_pinned<PlanInstance>("PlanInstance", sample_plan, pin_plan, s);
    ok = crc_pinned<TaskForceRec>("TaskForceRec", sample_tf, [&](StateHasher& h, const TaskForceRec& t) { pin_tf(h, t, st); }, s) && ok;
    ok = crc_pinned<StatusRec>("StatusRec", sample_status, pin_status, s) && ok;
    ok = crc_pinned<BuildTask>("BuildTask", sample_build, pin_build, s) && ok;
    ok = crc_pinned<PlanetBuildQueue>("PlanetBuildQueue", sample_queue, pin_queue, s) && ok;
    ok = crc_pinned<FreeStoreMove>("FreeStoreMove", sample_move, [&](StateHasher& h, const FreeStoreMove& m) { pin_move(h, m, st); }, s) && ok;
    // the saved-only records add nothing to the CRC
    ok = crc_pinned<PendingBonus>("PendingBonus", sample_bonus, [](StateHasher& h, const PendingBonus& x) { pin_nothing(h, x); }, s) && ok;
    ok = crc_pinned<PlayerAi::SourceList>("SourceList", sample_sources,
                                          [](StateHasher& h, const PlayerAi::SourceList& x) { pin_nothing(h, x); }, s) &&
         ok;
    ok = crc_pinned<PotentialPlanRec>("PotentialPlanRec", sample_pp, [](StateHasher& h, const PotentialPlanRec& x) { pin_nothing(h, x); }, s) &&
         ok;
    // a task force without a sync gather and a status of another kind hash as before q2f t09 / #119
    const auto no_sync = [](const Store& st2) {
        TaskForceRec t = sample_tf(st2);
        t.sync = false;
        return t;
    };
    ok = crc_pinned<TaskForceRec>("TaskForceRec without sync", no_sync, [&](StateHasher& h, const TaskForceRec& t) { pin_tf(h, t, st); }, s) &&
         ok;
    const auto produce = [](const Store& st2) {
        StatusRec x = sample_status(st2);
        x.kind = StatusKind::ProduceForce;
        return x;
    };
    ok = crc_pinned<StatusRec>("StatusRec ProduceForce", produce, pin_status, s) && ok;
    CHECK(ok);
}

// Save versions 6-10: the layouts GalacticAiWorld::read accepted before #145, read with the lacking fields at the
// record's defaults.
void test_old_versions(const Store& s) {
    const objects::ObjectStore& st = s.store;
    const TaskForceRec t = sample_tf(s);
    for (const uint32_t version : {6u, 7u}) {
        ByteWriter w;
        old_tf(w, t, st, version);
        TaskForceRec y;
        const bool ok = load(w.bytes(), s, y, version);
        const TaskForceRec d;
        std::printf("  task force, version %u: %s\n", version, ok ? "read" : "FAILED");
        CHECK(ok && y.stage == t.stage && y.area == t.area && y.alive == t.alive && y.fleet == d.fleet && y.units.size() == 1);
        CHECK(version >= 7 ? (y.sync == t.sync && y.sync_left == t.sync_left && y.sync_etas == t.sync_etas)
                           : (y.sync == d.sync && y.sync_left == d.sync_left && y.sync_etas.empty()));
    }
    {
        const StatusRec x = sample_status(s);
        ByteWriter w;
        old_status(w, x, 7);
        StatusRec y;
        CHECK(load(w.bytes(), s, y, 7) && y.fleet == StatusRec{}.fleet && y.dest == x.dest && y.amount == x.amount);
    }
    {
        const BuildTask x = sample_build(s);
        ByteWriter w;
        old_build(w, x, 8);
        BuildTask y;
        CHECK(load(w.bytes(), s, y, 8) && y.claimed == BuildTask{}.claimed && y.reclaimed && y.queue_id == x.queue_id);
    }
    {
        const PendingBonus x = sample_bonus(s);
        ByteWriter w;
        w.u32(x.goal_type).i32(x.target).f32(x.bonus).i64(x.deadline);  // version 9: no blocking status
        PendingBonus y;
        CHECK(load(w.bytes(), s, y, 9) && y.status == PendingBonus{}.status && y.deadline == x.deadline);
    }
    // the current image of every record is read by the current version only: an older reader stops short of its end
    {
        ByteWriter w;
        old_build(w, sample_build(s), 11);
        BuildTask y;
        CHECK(!load(w.bytes(), s, y, 8));
    }
}

void test_coverage(const Store& s) {
    size_t fields = 0;
    bool ok = coverage<PendingBonus>("PendingBonus", sample_bonus, s, fields);
    ok = coverage<PlayerAi::SourceList>("SourceList", sample_sources, s, fields) && ok;
    ok = coverage<PlanInstance>("PlanInstance", sample_plan, s, fields) && ok;
    ok = coverage<TaskForceRec>("TaskForceRec", sample_tf, s, fields, [](const TaskForceRec& t) -> const void* { return &t.sync; }) && ok;
    ok = coverage<StatusRec>("StatusRec", sample_status, s, fields, [](const StatusRec& x) -> const void* { return &x.kind; }) && ok;
    ok = coverage<PpSelection>("PpSelection", sample_selection, s, fields) && ok;
    ok = coverage<PotentialPlanRec>("PotentialPlanRec", sample_pp, s, fields) && ok;
    ok = coverage<BuildTask>("BuildTask", sample_build, s, fields) && ok;
    ok = coverage<QueueReservation>("QueueReservation", sample_reservation, s, fields) && ok;
    ok = coverage<PlanetBuildQueue>("PlanetBuildQueue", sample_queue, s, fields) && ok;
    ok = coverage<FreeStoreMove>("FreeStoreMove", sample_move, s, fields) && ok;
    std::printf("  coverage: %zu fields\n", fields);
    CHECK(ok && fields > 100);
}

}  // namespace

int main() {
    const Store s;
    alamo::test::section("round trip");
    test_round_trip(s);
    alamo::test::section("save pin (save version 11)");
    test_save_pin(s);
    alamo::test::section("CRC pin");
    test_crc_pin(s);
    alamo::test::section("older save versions");
    test_old_versions(s);
    alamo::test::section("field coverage");
    test_coverage(s);
    return alamo::test::report("alamo_ai_galactic_records_tests");
}
