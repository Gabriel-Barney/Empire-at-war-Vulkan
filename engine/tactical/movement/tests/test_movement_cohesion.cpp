// alamo_tactical_movement_cohesion_tests (no framework; non-zero exit on failure).
// [port: Gabriel 2026-11-13] Fleet cohesion (MoveOrder::cohesion_tight), not in retail: a tight space order caps every
// ship that is not a fighter at the order's slowest such ship's max speed, so the fleet keeps its spacing in flight. A
// loose order (the default, every AI / Lua move) is retail behaviour and hashes as before.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <utility>
#include <vector>

#include "alamo/core/bytes.hpp"
#include "alamo/core/sim_crc.hpp"
#include "alamo/core/sim_rng.hpp"
#include "alamo/tactical/movement/movement_system.hpp"
#include "alamo/test/support.hpp"
#include "alamo/test/thread_parallel_for.hpp"

using namespace alamo;
using namespace alamo::tactical::movement;

using ::alamo::test::g_fail;

namespace {

constexpr float kNone = 3.0e38f;

// Three frigates side by side (y = -300, 0, 300) at x = 0, speeds 2, 3 and 4, in open space; `fighter` adds a fighter.
void fleet(MovementSystem& sys, bool fighter = false) {
    sys.space().set_box({-5000.0f, -5000.0f}, {5000.0f, 5000.0f});
    const float speeds[3] = {2.0f, 3.0f, 4.0f};
    for (uint32_t i = 0; i < 3; ++i) {
        UnitDesc d;
        d.id = static_cast<ObjectId>(10 + i);
        d.pos = {0.0f, -300.0f + 300.0f * static_cast<float>(i)};
        d.max_speed = speeds[i];
        d.turn_rate = 2.0f;
        d.radius = 40.0f;
        d.space_layer = kLayerFrigate;
        d.space_layer_type = 2;
        d.type_name = "Frigate";
        CHECK(sys.add_unit(d));
    }
    if (fighter) {
        UnitDesc d;
        d.id = 20;
        d.pos = {-200.0f, 0.0f};
        d.max_speed = 8.0f;
        d.turn_rate = 6.0f;
        d.radius = 5.0f;
        d.space_layer = kLayerFighter;
        d.locomotor = LocomotorKind::Fighter;
        d.type_name = "Fighter";
        CHECK(sys.add_unit(d));
    }
}

MovementConfig space_cfg() {
    MovementConfig cfg;
    cfg.mode = MoveMode::Space;
    return cfg;
}

void order(MovementSystem& sys, std::vector<ObjectId> ids, bool tight, SimRng& rng, uint32_t frame = 0) {
    MoveOrder o;
    o.units = std::move(ids);
    o.destination = {3000.0f, 0.0f};
    o.flags = kMoveFlagPlayerOrder;
    o.cohesion_tight = tight;
    sys.move(o, rng, frame);
}

// The x spread (lead ship x - last ship x) of the three frigates after `frames` ticks.
float spread_after(bool tight, uint32_t frames) {
    MovementSystem sys{space_cfg()};
    fleet(sys);
    SimRng rng(7);
    order(sys, {10, 11, 12}, tight, rng);
    for (uint32_t f = 0; f < frames; ++f) sys.tick(f, rng);
    float lo = 3.0e38f, hi = -3.0e38f;
    for (ObjectId id : {10, 11, 12}) {
        lo = std::min(lo, sys.view(id).pos.x);
        hi = std::max(hi, sys.view(id).pos.x);
    }
    return hi - lo;
}

void test_cap() {
    MovementSystem sys{space_cfg()};
    fleet(sys, true);
    SimRng rng(7);
    // Loose: no cap on anyone (retail).
    order(sys, {10, 11, 12, 20}, false, rng);
    bool none = true;
    for (ObjectId id : {10, 11, 12, 20}) none = none && sys.view(id).cohesion_speed == kNone;
    CHECK(none);
    // Tight: the frigates share the slowest frigate's effective max speed; the fighter keeps its own flight.
    order(sys, {10, 11, 12, 20}, true, rng);
    const float slow = sys.view(10).max_speed;
    CHECK(slow < sys.view(11).max_speed && slow < sys.view(12).max_speed);
    bool capped = true;
    for (ObjectId id : {10, 11, 12}) capped = capped && sys.view(id).cohesion_speed == slow;
    CHECK(capped);
    CHECK(sys.view(20).cohesion_speed == kNone);
    // A tight order of one ship: no cap. A stop ends the cap.
    order(sys, {12}, true, rng);
    CHECK(sys.view(12).cohesion_speed == kNone && sys.view(10).cohesion_speed == slow);
    const ObjectId stop_ids[] = {10};
    sys.stop(stop_ids);
    CHECK(sys.view(10).cohesion_speed == kNone && sys.view(11).cohesion_speed == slow);
    // A later loose order clears it.
    order(sys, {11}, false, rng);
    CHECK(sys.view(11).cohesion_speed == kNone);
    std::printf("cohesion cap: slowest %.3f\n", static_cast<double>(slow));
}

void test_spacing() {
    // After 400 frames a loose fleet has strung out (speeds 2 / 3 / 4 x the mode multiplier); a tight one has not.
    const float loose = spread_after(false, 400), tight = spread_after(true, 400);
    std::printf("cohesion spread after 400 frames: loose %.1f, tight %.1f\n", static_cast<double>(loose), static_cast<double>(tight));
    CHECK(loose > 500.0f);
    CHECK(tight < 60.0f);
}

void test_arrival() {
    // A tight fleet still arrives, every ship at its slot.
    MovementSystem sys{space_cfg()};
    fleet(sys);
    SimRng rng(7);
    order(sys, {10, 11, 12}, true, rng);
    uint32_t f = 0;
    for (; f < 6000; ++f) {
        sys.tick(f, rng);
        bool any = false;
        for (ObjectId id : {10, 11, 12}) any = any || sys.view(id).has_goal;
        if (!any) break;
    }
    bool arrived = true;
    for (ObjectId id : {10, 11, 12}) arrived = arrived && sys.view(id).arrived && distance(sys.view(id).pos, sys.view(id).goal) < 1.0f;
    std::printf("cohesion arrival: %u frames\n", f);
    CHECK(arrived && f < 6000);
}

uint64_t speed_hash(const MovementSystem& sys) {
    StateHasher h;
    sys.hash_speed(h);
    return h.digest();
}

void test_hash_and_save() {
    // A loose order adds nothing to the speed hash (the goldens keep their CRCs); a tight one does.
    MovementSystem a{space_cfg()}, b{space_cfg()};
    fleet(a);
    fleet(b);
    const uint64_t before = speed_hash(a);
    SimRng ra(7), rb(7);
    order(a, {10, 11, 12}, false, ra);
    CHECK(speed_hash(a) == before);
    order(b, {10, 11, 12}, true, rb);
    CHECK(speed_hash(b) != before);
    for (uint32_t f = 0; f < 50; ++f) b.tick(f, rb);
    // Save / load keeps the cap, and the loaded run continues bit for bit.
    ByteWriter w;
    b.write(w);
    MovementSystem c{space_cfg()};
    fleet(c);
    ByteReader r(w.bytes());
    CHECK(c.read(r) && r.ok() && r.done());
    CHECK(c.view(12).cohesion_speed == b.view(12).cohesion_speed && c.view(12).cohesion_speed < kNone);
    SimRng rc = rb;
    for (uint32_t f = 50; f < 200; ++f) {
        b.tick(f, rb);
        c.tick(f, rc);
    }
    bool same = true;
    for (ObjectId id : {10, 11, 12}) same = same && b.view(id).pos.x == c.view(id).pos.x && b.view(id).pos.y == c.view(id).pos.y;
    CHECK(same);
}

// #714 (ShouldUseSpaceIdleMovement / IdleMovementFrames, 0x420a40): frigate 10 flies alone to (3000, 0) (the others
// never move, so they are never arrived). Off (FoC): it never moves again. On: its first drift starts within
// [arrival + F, arrival + 1.5 F + 1] frames, each drift goal lies ahead along its facing, and it drifts again after each
// idle wait; a save during the first wait continues bit for bit, and an order during a wait replaces it.
// `speed` is SpaceIdleMovementSpeed, the drift's minimum speed.
struct DriftRun {
    uint32_t arrived_at = 0;
    uint32_t first_drift = 0xFFFFFFFFu;  // the first frame it has a goal again
    uint32_t drifts = 0;                 // drift moves started in the 400 frames after the arrival
    float moved = 0.0f;                  // its distance from the arrival point at the end
    bool ahead = true;                   // every drift goal lies ahead along its facing at the drift's start
    bool others_still = true;            // frigates 11 / 12 (never ordered) do not move
    bool saved_same = true;              // the run loaded at arrival + 10 ends where this one does
    bool order_replaces = false;         // an order during a wait is the ship's goal
};
DriftRun drift_run(bool on, float frames, float speed) {
    MovementConfig cfg = space_cfg();
    cfg.constants.should_use_space_idle_movement = on;
    cfg.constants.idle_movement_frames = frames;
    cfg.constants.space_idle_movement_speed = speed;
    MovementSystem sys{cfg};
    fleet(sys);
    SimRng rng(7);
    order(sys, {10}, false, rng);
    DriftRun out;
    uint32_t f = 0;
    for (; f < 6000 && sys.view(10).has_goal; ++f) sys.tick(f, rng);
    out.arrived_at = f;
    const V2 at = sys.view(10).pos, still11 = sys.view(11).pos, still12 = sys.view(12).pos;
    std::unique_ptr<MovementSystem> copy;
    SimRng copy_rng = rng;
    bool had = false;
    for (uint32_t g = f; g < f + 400; ++g) {
        if (g == f + 10) {  // the first wait is running: save and load into a fresh system
            ByteWriter w;
            sys.write(w);
            copy = std::make_unique<MovementSystem>(cfg);
            fleet(*copy);
            ByteReader r(w.bytes());
            CHECK(copy->read(r) && r.ok() && r.done());
            copy_rng = rng;
        }
        sys.tick(g, rng);
        if (copy) copy->tick(g, copy_rng);
        const auto v = sys.view(10);
        if (v.has_goal && !had) {
            ++out.drifts;
            if (out.first_drift == 0xFFFFFFFFu) out.first_drift = g;
            const V2 fwd = dir_from_deg(v.yaw);
            out.ahead = out.ahead && (v.goal.x - v.pos.x) * fwd.x + (v.goal.y - v.pos.y) * fwd.y > 0.0f;
        }
        had = v.has_goal;
    }
    out.moved = distance(sys.view(10).pos, at);
    out.others_still = sys.view(11).pos.x == still11.x && sys.view(12).pos.x == still12.x;
    if (copy) out.saved_same = copy->view(10).pos.x == sys.view(10).pos.x && copy->view(10).pos.y == sys.view(10).pos.y;
    MoveOrder o;
    o.units = {10};
    o.destination = {-2000.0f, 0.0f};
    sys.move(o, rng, f + 400);
    out.order_replaces = sys.view(10).has_goal && sys.view(10).goal.x == -2000.0f;
    return out;
}

// ledger: verifies GameConstants.ShouldUseSpaceIdleMovement
void test_idle_drift() {
    const DriftRun off = drift_run(false, 30.0f, 0.0f), on = drift_run(true, 30.0f, 0.0f), slow = drift_run(true, 60.0f, 6.0f);
    for (const auto& [name, r] : {std::pair{"off", &off}, std::pair{"on (30)", &on}, std::pair{"on (60, speed 6)", &slow}})
        std::printf("idle drift %s: arrived %u, first drift %+d, drifts %u, moved %.2f, ahead %d, others still %d, saved same %d, "
                    "order replaces %d\n",
                    name, r->arrived_at, r->first_drift == 0xFFFFFFFFu ? -1 : static_cast<int>(r->first_drift - r->arrived_at),
                    r->drifts, static_cast<double>(r->moved), r->ahead ? 1 : 0, r->others_still ? 1 : 0, r->saved_same ? 1 : 0,
                    r->order_replaces ? 1 : 0);
    CHECK(off.first_drift == 0xFFFFFFFFu && off.drifts == 0 && off.moved == 0.0f && off.saved_same && off.order_replaces);
    CHECK(on.first_drift >= on.arrived_at + 30 && on.first_drift <= on.arrived_at + 46 && on.drifts >= 2 && on.moved > 1.0f && on.ahead &&
          on.others_still && on.saved_same && on.order_replaces);
    CHECK(slow.first_drift >= slow.arrived_at + 60 && slow.first_drift <= slow.arrived_at + 91 && slow.drifts >= 1 && slow.ahead &&
          slow.others_still && slow.saved_same);
}

void test_threads() {
    // Rule 3: the tight run is the same with 1 and 4 workers.
    auto run = [](const ParallelFor& pf) {
        MovementSystem sys{space_cfg()};
        fleet(sys, true);
        SimRng rng(7);
        order(sys, {10, 11, 12, 20}, true, rng);
        for (uint32_t f = 0; f < 300; ++f) sys.tick(f, rng, pf);
        StateHasher h;
        sys.hash_units(h);
        sys.hash_speed(h);
        return h.digest();
    };
    CHECK(run(serial_for) == run(::alamo::test::make_thread_parallel_for(4)));
}

}  // namespace

int main() {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    test_cap();
    test_spacing();
    test_arrival();
    test_hash_and_save();
    test_idle_drift();  // #714
    test_threads();
    if (g_fail) {
        std::printf("FAILED: %d\n", g_fail);
        return 1;
    }
    std::printf("all cohesion tests passed\n");
    return 0;
}
