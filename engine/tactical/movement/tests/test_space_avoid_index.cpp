// alamo_tactical_movement_avoid_index_tests (no framework; non-zero exit on failure).
// #111: the triangle index of an avoidance mesh (avoid_build_index, space_avoid.hpp) changes which triangles a segment
// visits, never a result: avoid_precise, avoid_precise_segment (nearest t and its mesh) and avoid_ray_any answer exactly
// as the plain every-triangle loop does, for random segments, segments through vertices and along edges, axis-parallel
// and zero-length segments, starts inside the mesh box, and far-away segments.
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

#include "alamo/tactical/movement/space_avoid.hpp"
#include "alamo/test/support.hpp"

using namespace alamo;
using namespace alamo::tactical::movement;

using ::alamo::test::g_fail;

namespace {

uint32_t g_state = 0x9E3779B9u;
float rnd(float lo, float hi) {  // xorshift32: a fixed sequence, no dependence on the sim RNG
    g_state ^= g_state << 13;
    g_state ^= g_state >> 17;
    g_state ^= g_state << 5;
    return lo + (hi - lo) * static_cast<float>(g_state & 0xFFFFFFu) / static_cast<float>(0xFFFFFFu);
}

void add_tri(AvoidModel::Mesh& m, F3 a, F3 b, F3 c) {
    for (const F3 p : {a, b, c}) {
        if (m.tris.empty()) m.lo = m.hi = p;
        m.lo = {std::fmin(m.lo.x, p.x), std::fmin(m.lo.y, p.y), std::fmin(m.lo.z, p.z)};
        m.hi = {std::fmax(m.hi.x, p.x), std::fmax(m.hi.y, p.y), std::fmax(m.hi.z, p.z)};
        m.tris.push_back(p);
    }
}

// A lumpy sphere (an asteroid: `rings` x `segs` quads) of radius about r around c.
void add_rock(AvoidModel::Mesh& m, F3 c, float r, int32_t rings, int32_t segs) {
    auto at = [&](int32_t i, int32_t j) {
        const float th = 3.14159265f * static_cast<float>(i) / static_cast<float>(rings);
        const float ph = 6.28318531f * static_cast<float>(j % segs) / static_cast<float>(segs);
        const float rr = r * (1.0f + 0.15f * std::sin(3.0f * ph) * std::sin(2.0f * th));
        return F3{c.x + rr * std::sin(th) * std::cos(ph), c.y + rr * std::sin(th) * std::sin(ph), c.z + rr * std::cos(th)};
    };
    for (int32_t i = 0; i < rings; ++i)
        for (int32_t j = 0; j < segs; ++j) {
            add_tri(m, at(i, j), at(i + 1, j), at(i + 1, j + 1));
            add_tri(m, at(i, j), at(i + 1, j + 1), at(i, j + 1));
        }
}

bool same_bits(float a, float b) { return std::memcmp(&a, &b, sizeof a) == 0; }

}  // namespace

int main() {
    AvoidModel plain;
    {
        AvoidModel::Mesh rock;
        rock.name = "ROCK";
        add_rock(rock, {0.0f, 0.0f, 0.0f}, 400.0f, 48, 96);  // 9216 triangles
        plain.meshes.push_back(rock);
        AvoidModel::Mesh shards;  // scattered loose triangles, some degenerate
        shards.name = "SHARDS";
        for (int32_t i = 0; i < 300; ++i) {
            const F3 a{rnd(-600.0f, 600.0f), rnd(-600.0f, 600.0f), rnd(-200.0f, 200.0f)};
            const F3 b{a.x + rnd(-40.0f, 40.0f), a.y + rnd(-40.0f, 40.0f), a.z + rnd(-40.0f, 40.0f)};
            const F3 c = i % 37 == 0 ? b : F3{a.x + rnd(-40.0f, 40.0f), a.y + rnd(-40.0f, 40.0f), a.z + rnd(-40.0f, 40.0f)};
            add_tri(shards, a, b, c);
        }
        plain.meshes.push_back(shards);
        AvoidModel::Mesh shield;
        shield.name = "SHIELD";
        shield.shield = true;
        add_rock(shield, {0.0f, 0.0f, 0.0f}, 520.0f, 12, 24);
        plain.meshes.push_back(shield);
        AvoidModel::Mesh small;  // under the index's leaf size: no nodes, the plain loop
        small.name = "SMALL";
        add_tri(small, {700.0f, 0.0f, 0.0f}, {710.0f, 0.0f, 0.0f}, {700.0f, 10.0f, 0.0f});
        plain.meshes.push_back(small);
        plain.bound_c = {0.0f, 0.0f, 0.0f};
        plain.bound_h = {720.0f, 720.0f, 520.0f};
    }
    AvoidModel indexed = plain;
    avoid_build_index(indexed);
    CHECK(!indexed.meshes[0].nodes.empty() && indexed.meshes[0].order.size() * 3 == indexed.meshes[0].tris.size());
    CHECK(indexed.meshes[3].nodes.empty());
    for (const AvoidModel::Mesh& m : plain.meshes) CHECK(m.nodes.empty());

    AvoidObstacle po;  // a turned, scaled, offset obstacle: the world -> model transform is exercised too
    po.id = 7;
    po.pos = {1500.0f, -800.0f, 30.0f};
    po.facing = {0.6f, 0.8f};
    po.scale = 1.7f;
    po.model = &plain;
    avoid_place(po);
    AvoidObstacle io = po;
    io.model = &indexed;
    AvoidObstacle po_shield = po, io_shield = io;
    po_shield.skip_shield = io_shield.skip_shield = true;

    // Segments: random through and around the rock, through its vertices (hit exactly at a vertex), along its edges,
    // axis-parallel, zero-length, and far away.
    std::vector<std::pair<F3, F3>> segs;
    for (int32_t i = 0; i < 4000; ++i) {
        const F3 a{rnd(-900.0f, 900.0f), rnd(-900.0f, 900.0f), rnd(-700.0f, 700.0f)};
        const float len = i % 3 == 0 ? rnd(1.0f, 60.0f) : rnd(60.0f, 1600.0f);
        const F3 d{rnd(-1.0f, 1.0f), rnd(-1.0f, 1.0f), rnd(-1.0f, 1.0f)};
        const float dl = std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z) + 1e-6f;
        segs.push_back({a, {a.x + d.x / dl * len, a.y + d.y / dl * len, a.z + d.z / dl * len}});
    }
    const std::vector<F3>& rt = plain.meshes[0].tris;
    for (size_t i = 0; i < rt.size(); i += 97) {
        const F3 v = rt[i], w = rt[i + 1];
        const F3 d{rnd(-50.0f, 50.0f), rnd(-50.0f, 50.0f), rnd(-50.0f, 50.0f)};
        segs.push_back({{v.x - d.x, v.y - d.y, v.z - d.z}, {v.x + d.x, v.y + d.y, v.z + d.z}});  // through a vertex
        segs.push_back({v, w});                                                                   // along an edge
        segs.push_back({{v.x - 2.0f * (w.x - v.x), v.y - 2.0f * (w.y - v.y), v.z - 2.0f * (w.z - v.z)},
                        {v.x + 2.0f * (w.x - v.x), v.y + 2.0f * (w.y - v.y), v.z + 2.0f * (w.z - v.z)}});  // edge line
        segs.push_back({{v.x, v.y, v.z - 900.0f}, {v.x, v.y, v.z + 900.0f}});                     // axis-parallel
        segs.push_back({v, v});                                                                   // zero length
    }
    for (int32_t i = 0; i < 200; ++i) {
        const F3 a{rnd(-5.0e4f, 5.0e4f), rnd(-5.0e4f, 5.0e4f), rnd(-5.0e3f, 5.0e3f)};
        segs.push_back({a, {a.x + rnd(-3.0e4f, 3.0e4f), a.y + rnd(-3.0e4f, 3.0e4f), a.z + rnd(-3.0e3f, 3.0e3f)}});  // far / long
    }

    uint32_t hits = 0, seg_hits = 0, ray_hits = 0, mismatches = 0;
    for (const auto& [m0, m1] : segs) {
        const F3 a = avoid_to_world(po, m0), b = avoid_to_world(po, m1);
        for (int32_t pass = 0; pass < 2; ++pass) {
            const AvoidObstacle& P = pass == 0 ? po : po_shield;
            const AvoidObstacle& I = pass == 0 ? io : io_shield;
            bool pin = false, iin = false;
            const bool ph = avoid_precise(P, a, b, pin), ih = avoid_precise(I, a, b, iin);
            hits += ph ? 1u : 0u;
            if (ph != ih || pin != iin) ++mismatches;
            for (const uint32_t mask : {1u, 5u, 7u}) {
                AvoidSegmentHit hp, hi;
                const bool sp = avoid_precise_segment(P, a, b, mask, 2u, hp), si = avoid_precise_segment(I, a, b, mask, 2u, hi);
                seg_hits += sp ? 1u : 0u;
                if (sp != si || hp.mesh != hi.mesh || !same_bits(hp.t, hi.t)) ++mismatches;
            }
            const bool rp = avoid_ray_any(P, a, b), ri = avoid_ray_any(I, a, b);
            ray_hits += rp ? 1u : 0u;
            if (rp != ri) ++mismatches;
        }
    }
    CHECK(mismatches == 0);
    CHECK(hits > 500 && seg_hits > 1500 && ray_hits > 500);  // the sample does hit the model
    std::printf("avoid index: %zu segments, %u precise hits, %u segment hits, %u ray hits, %u mismatches\n", segs.size(), hits, seg_hits,
                ray_hits, mismatches);

    if (g_fail) {
        std::printf("alamo_tactical_movement_avoid_index_tests: %d failure(s)\n", g_fail);
        return 1;
    }
    std::printf("alamo_tactical_movement_avoid_index_tests: all passed\n");
    return 0;
}
