// Space avoidance tree and precise model test (legacy docs/re/SPACE_AVOIDANCE.md). See space_avoid.hpp.
#include "alamo/tactical/movement/space_avoid.hpp"

#include <algorithm>
#include <cmath>

namespace alamo::tactical::movement {

namespace {
float avoid_dot(F3 a, F3 b) { return (a.x * b.x + a.y * b.y) + a.z * b.z; }
F3 avoid_cross(F3 a, F3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
float avoid_axis(F3 v, int32_t i) { return i == 0 ? v.x : (i == 1 ? v.y : v.z); }
}  // namespace

void avoid_place(AvoidObstacle& o) {
    float fx = o.facing.x, fy = o.facing.y;
    const float len2 = fx * fx + fy * fy;
    if (!(len2 > 0.0f)) {
        fx = 1.0f;
        fy = 0.0f;
    } else {
        const float inv = 1.0f / std::sqrt(len2);
        fx = fx * inv;
        fy = fy * inv;
    }
    o.facing = {fx, fy};
    if (!o.model) {  // §3.1 case 2: [P, P + 0.1]
        const F3 q{o.pos.x + 0.1f, o.pos.y + 0.1f, o.pos.z + 0.1f};
        o.box_c = {(q.x + o.pos.x) * 0.5f, (q.y + o.pos.y) * 0.5f, (q.z + o.pos.z) * 0.5f};
        o.box_h = {std::fabs(q.x - o.pos.x) * 0.5f, std::fabs(q.y - o.pos.y) * 0.5f, std::fabs(q.z - o.pos.z) * 0.5f};
        return;
    }
    // §3.1 case 3: centre_i = M_i0 c.x + M_i1 c.y + M_i2 c.z + M_i3, half_i = |M_i0 h.x| + |M_i1 h.y| + |M_i2 h.z|.
    const float s = o.scale;
    const float m[3][3] = {{-fy * s, -fx * s, 0.0f}, {fx * s, -fy * s, 0.0f}, {0.0f, 0.0f, s}};
    const F3 c = o.model->bound_c, h = o.model->bound_h;
    const float p[3] = {o.pos.x, o.pos.y, o.pos.z};
    float bc[3], bh[3];
    for (int32_t i = 0; i < 3; ++i) {
        bc[i] = ((m[i][0] * c.x + m[i][1] * c.y) + m[i][2] * c.z) + p[i];
        bh[i] = (std::fabs(m[i][0] * h.x) + std::fabs(m[i][1] * h.y)) + std::fabs(m[i][2] * h.z);
    }
    o.box_c = {bc[0], bc[1], bc[2]};
    o.box_h = {bh[0], bh[1], bh[2]};
}

F3 avoid_to_world(const AvoidObstacle& o, F3 m) {
    const float s = o.scale, fx = o.facing.x, fy = o.facing.y;
    return {(-fy * s) * m.x + (-fx * s) * m.y + o.pos.x, (fx * s) * m.x + (-fy * s) * m.y + o.pos.y, s * m.z + o.pos.z};
}

F3 avoid_to_model(const AvoidObstacle& o, F3 w) {
    const float s = o.scale, fx = o.facing.x, fy = o.facing.y;
    const F3 d{w.x - o.pos.x, w.y - o.pos.y, w.z - o.pos.z};
    return {(-fy * d.x + fx * d.y) / s, (-fx * d.x - fy * d.y) / s, d.z / s};
}

namespace {
// 0x20c850 with the fraction: see avoid_segment_box; `t_out` = the hit fraction (0 for a start inside).
bool segment_box_t(F3 o, F3 d, F3 lo, F3 hi, float t_best, bool& inside, float& t_out) {
    if (o.x >= lo.x && o.x <= hi.x && o.y >= lo.y && o.y <= hi.y && o.z >= lo.z && o.z <= hi.z) {
        inside = true;
        t_out = 0.0f;
        return true;
    }
    float t = -1.0f;
    int32_t axis = -1;
    for (int32_t i = 0; i < 3; ++i) {
        const float oi = avoid_axis(o, i), di = avoid_axis(d, i), l = avoid_axis(lo, i), hh = avoid_axis(hi, i);
        float ti = -1.0f;
        if (di != 0.0f) {
            if (oi < l) ti = (l - oi) / di;
            else if (oi > hh) ti = (hh - oi) / di;
        }
        if (axis < 0 || ti > t) {  // the largest t, first maximum
            t = ti;
            axis = i;
        }
    }
    if (t < 0.0f) return false;
    for (int32_t i = 0; i < 3; ++i) {
        if (i == axis) continue;
        const float q = avoid_axis(o, i) + avoid_axis(d, i) * t;
        if (q < avoid_axis(lo, i) || q > avoid_axis(hi, i)) return false;
    }
    t_out = t;
    return t < t_best;
}

// 0x20c410 with the fraction: see avoid_segment_triangle.
bool segment_triangle_t(F3 o, F3 d, F3 a, F3 b, F3 c, float t_best, float& t_out) {
    const F3 e1 = f3_sub(b, a), e2 = f3_sub(c, a);
    const F3 pv = avoid_cross(d, e2);
    const float det = avoid_dot(e1, pv);
    if (!(std::fabs(det) >= 1e-4f)) return false;
    const float inv = 1.0f / det;
    const F3 tv = f3_sub(o, a);
    const float u = avoid_dot(tv, pv) * inv;
    if (u < 0.0f || u > 1.0f) return false;
    const F3 qv = avoid_cross(tv, e1);
    const float v = avoid_dot(d, qv) * inv;
    if (v < 0.0f || u + v > 1.0f) return false;
    const float t = avoid_dot(e2, qv) * inv;
    if (!(t > 0.0f && t < t_best)) return false;
    t_out = t;
    return true;
}
}  // namespace

namespace {
// #111 triangle index (AvoidModel::Mesh::nodes). Leaves hold at most kLeafTris triangles; a mesh of no more is not indexed.
constexpr uint32_t kLeafTris = 8;

struct IndexTri {
    F3 lo, hi, c;  // box and centre of the triangle
    uint32_t tri;  // its number in Mesh::tris / 3
};

float axis_of(F3 v, int32_t a) { return a == 0 ? v.x : (a == 1 ? v.y : v.z); }

// Node `node` over tris[b, e): its box; a leaf when few enough (or every centre equal), else split at the median centre
// along the longest centre extent (ties by triangle number, so the build is deterministic).
void build_node(AvoidModel::Mesh& m, std::vector<IndexTri>& t, uint32_t node, uint32_t b, uint32_t e) {
    F3 lo = t[b].lo, hi = t[b].hi, clo = t[b].c, chi = t[b].c;
    for (uint32_t i = b + 1; i < e; ++i) {
        lo = {std::min(lo.x, t[i].lo.x), std::min(lo.y, t[i].lo.y), std::min(lo.z, t[i].lo.z)};
        hi = {std::max(hi.x, t[i].hi.x), std::max(hi.y, t[i].hi.y), std::max(hi.z, t[i].hi.z)};
        clo = {std::min(clo.x, t[i].c.x), std::min(clo.y, t[i].c.y), std::min(clo.z, t[i].c.z)};
        chi = {std::max(chi.x, t[i].c.x), std::max(chi.y, t[i].c.y), std::max(chi.z, t[i].c.z)};
    }
    m.nodes[node].lo = lo;
    m.nodes[node].hi = hi;
    const F3 ext{chi.x - clo.x, chi.y - clo.y, chi.z - clo.z};
    const int32_t axis = ext.x >= ext.y && ext.x >= ext.z ? 0 : (ext.y >= ext.z ? 1 : 2);
    if (e - b <= kLeafTris || !(axis_of(ext, axis) > 0.0f)) {
        m.nodes[node].first = static_cast<uint32_t>(m.order.size());
        m.nodes[node].count = e - b;
        for (uint32_t i = b; i < e; ++i) m.order.push_back(t[i].tri);
        return;
    }
    const uint32_t mid = b + (e - b) / 2;
    std::nth_element(t.begin() + b, t.begin() + mid, t.begin() + e, [axis](const IndexTri& x, const IndexTri& y) {
        const float cx = axis_of(x.c, axis), cy = axis_of(y.c, axis);
        return cx < cy || (cx == cy && x.tri < y.tri);
    });
    const auto child = static_cast<uint32_t>(m.nodes.size());
    m.nodes.resize(m.nodes.size() + 2);  // indices only from here: the resize moves the nodes
    m.nodes[node].first = child;
    m.nodes[node].count = 0;
    build_node(m, t, child, b, mid);
    build_node(m, t, child + 1, mid, e);
}

// Whether the segment o + t d, t in [0, 1], can meet [lo, hi] grown by `margin` on every side (slab test in double). A
// triangle the exact tests hit has its hit point on the segment and, up to their float rounding, inside its own box: the
// margin covers that rounding with a wide factor, so no hit is ever culled.
bool segment_may_touch(F3 o, F3 d, F3 lo, F3 hi, double margin) {
    double t0 = 0.0, t1 = 1.0;
    for (int32_t a = 0; a < 3; ++a) {
        const double oa = axis_of(o, a), da = axis_of(d, a);
        const double l = static_cast<double>(axis_of(lo, a)) - margin, h = static_cast<double>(axis_of(hi, a)) + margin;
        if (da == 0.0) {
            if (oa < l || oa > h) return false;
            continue;
        }
        double ta = (l - oa) / da, tb = (h - oa) / da;
        if (ta > tb) std::swap(ta, tb);
        t0 = std::max(t0, ta);
        t1 = std::min(t1, tb);
        if (t0 > t1) return false;
    }
    return true;
}

// Calls visit(i) for the triangles of `mesh` (number i: points tris[3i .. 3i + 2]) that the segment o -> o + d may hit,
// stopping when visit returns true (then true). No index: every triangle in order.
template <class Visit>
bool visit_triangles(const AvoidModel::Mesh& mesh, F3 o, F3 d, Visit&& visit) {
    if (mesh.nodes.empty()) {
        for (size_t i = 0; i + 2 < mesh.tris.size(); i += 3)
            if (visit(static_cast<uint32_t>(i / 3))) return true;
        return false;
    }
    const auto mag = [](F3 v) { return std::max(std::fabs(v.x), std::max(std::fabs(v.y), std::fabs(v.z))); };
    const F3 root_ext{mesh.nodes[0].hi.x - mesh.nodes[0].lo.x, mesh.nodes[0].hi.y - mesh.nodes[0].lo.y,
                      mesh.nodes[0].hi.z - mesh.nodes[0].lo.z};
    const double margin = 1e-4 * (static_cast<double>(mag(o)) + mag(d) + mag(root_ext) + mag(mesh.nodes[0].lo)) + 1e-3;
    uint32_t stack[128];
    uint32_t top = 0;
    stack[top++] = 0;
    while (top > 0) {
        const AvoidModel::Mesh::Node& n = mesh.nodes[stack[--top]];
        if (!segment_may_touch(o, d, n.lo, n.hi, margin)) continue;
        if (n.count > 0) {
            for (uint32_t k = n.first; k < n.first + n.count; ++k)
                if (visit(mesh.order[k])) return true;
            continue;
        }
        if (top + 2 > 128) {  // [port] deeper than any median split of a 32-bit count: the subtree's triangles one by one
            for (size_t i = 0; i + 2 < mesh.tris.size(); i += 3)
                if (visit(static_cast<uint32_t>(i / 3))) return true;
            return false;
        }
        stack[top++] = n.first + 1;
        stack[top++] = n.first;
    }
    return false;
}
}  // namespace

void avoid_build_index(AvoidModel& model) {
    for (AvoidModel::Mesh& m : model.meshes) {
        m.nodes.clear();
        m.order.clear();
        const auto count = static_cast<uint32_t>(m.tris.size() / 3);
        if (count <= kLeafTris) continue;
        std::vector<IndexTri> t(count);
        for (uint32_t i = 0; i < count; ++i) {
            const F3 a = m.tris[3 * i], b = m.tris[3 * i + 1], c = m.tris[3 * i + 2];
            t[i].lo = {std::min({a.x, b.x, c.x}), std::min({a.y, b.y, c.y}), std::min({a.z, b.z, c.z})};
            t[i].hi = {std::max({a.x, b.x, c.x}), std::max({a.y, b.y, c.y}), std::max({a.z, b.z, c.z})};
            t[i].c = {(t[i].lo.x + t[i].hi.x) * 0.5f, (t[i].lo.y + t[i].hi.y) * 0.5f, (t[i].lo.z + t[i].hi.z) * 0.5f};
            t[i].tri = i;
        }
        m.order.reserve(count);
        m.nodes.resize(1);
        build_node(m, t, 0, 0, count);
    }
}

bool avoid_segment_box(F3 o, F3 d, F3 lo, F3 hi, float t_best, bool& inside) {
    float t = 0.0f;
    return segment_box_t(o, d, lo, hi, t_best, inside, t);
}

bool avoid_segment_triangle(F3 o, F3 d, F3 a, F3 b, F3 c, float t_best) {
    float t = 0.0f;
    return segment_triangle_t(o, d, a, b, c, t_best, t);
}

bool avoid_precise(const AvoidObstacle& o, F3 a, F3 b, bool& inside) {
    if (!o.model) return false;
    const AvoidModel& m = *o.model;
    const F3 ma = avoid_to_model(o, a), mb = avoid_to_model(o, b);
    const F3 d = f3_sub(mb, ma);
    if (m.has_collision_box) {  // §5.3: the box path only (the collide switch is not consulted)
        const F3 lo = f3_sub(m.cbox_c, m.cbox_h), hi = f3_add(m.cbox_c, m.cbox_h);
        return avoid_segment_box(ma, d, lo, hi, 1.0f, inside);
    }
    if (!o.collide) return false;  // §5.5
    for (const AvoidModel::Mesh& mesh : m.meshes) {  // ALO order; hits are ORed, any one decides the boolean
        if (mesh.shield && o.skip_shield) continue;
        bool private_inside = false;  // the broad phase's own record
        if (!avoid_segment_box(ma, d, mesh.lo, mesh.hi, 1.0f, private_inside)) continue;
        const std::vector<F3>& tr = mesh.tris;
        if (visit_triangles(mesh, ma, d, [&](uint32_t i) { return avoid_segment_triangle(ma, d, tr[3 * i], tr[3 * i + 1], tr[3 * i + 2], 1.0f); }))
            return true;
    }
    return false;
}

bool space_avoid(std::span<const AvoidObstacle> members, F3 centre, F3 half, F3 a, F3 b, F3& hit_pos) {
    bool inside = false;  // hit-record byte 1: shared across candidates, never reset (§5.7)
    for (const AvoidObstacle& o : members) {
        // §4 overlap (0x20bb60): disjoint iff |c_q - c_l| > h_q + h_l on some axis.
        if (std::fabs(centre.x - o.box_c.x) > half.x + o.box_h.x || std::fabs(centre.y - o.box_c.y) > half.y + o.box_h.y ||
            std::fabs(centre.z - o.box_c.z) > half.z + o.box_h.z)
            continue;
        if (avoid_precise(o, a, b, inside) && !inside) {
            hit_pos = o.pos;
            return true;
        }
    }
    return false;
}

bool avoid_precise_segment(const AvoidObstacle& o, F3 a, F3 b, uint32_t mask, uint32_t shield_mask, AvoidSegmentHit& hit) {
    if (!o.model) return false;
    const AvoidModel& m = *o.model;
    const F3 ma = avoid_to_model(o, a), mb = avoid_to_model(o, b);
    const F3 d = f3_sub(mb, ma);
    if (m.has_collision_box) {
        const F3 lo = f3_sub(m.cbox_c, m.cbox_h), hi = f3_add(m.cbox_c, m.cbox_h);
        bool inside = false;
        float t = 0.0f;
        if (!segment_box_t(ma, d, lo, hi, hit.t, inside, t)) return false;
        hit.t = t;
        hit.mesh = -1;
        return true;
    }
    if (!o.collide) return false;
    bool any = false;
    for (size_t k = 0; k < m.meshes.size(); ++k) {  // ALO order, one shared record: the nearest hit survives
        const AvoidModel::Mesh& mesh = m.meshes[k];
        const uint32_t mesh_mask = (mesh.shield && o.skip_shield) ? shield_mask : 1u;
        if ((mask & mesh_mask) == 0) continue;
        bool private_inside = false;
        float tb = 0.0f;
        if (!segment_box_t(ma, d, mesh.lo, mesh.hi, 1.0f, private_inside, tb)) continue;
        const std::vector<F3>& tr = mesh.tris;
        visit_triangles(mesh, ma, d, [&](uint32_t i) {  // the nearest t: no visiting order changes the minimum
            float t = 0.0f;
            if (segment_triangle_t(ma, d, tr[3 * i], tr[3 * i + 1], tr[3 * i + 2], hit.t, t)) {
                hit.t = t;
                hit.mesh = static_cast<int32_t>(k);
                any = true;
            }
            return false;
        });
    }
    return any;
}

bool avoid_ray_any(const AvoidObstacle& o, F3 a, F3 b) {
    if (!o.model) return false;
    const F3 ma = avoid_to_model(o, a), mb = avoid_to_model(o, b);
    const F3 d = f3_sub(mb, ma);
    for (const AvoidModel::Mesh& mesh : o.model->meshes) {  // ORed: any hit decides
        if (!mesh.bone_visible || (mesh.shield && o.skip_shield)) continue;
        bool inside = false;
        float tb = 0.0f;
        if (!segment_box_t(ma, d, mesh.lo, mesh.hi, 1.0f, inside, tb)) continue;  // AABB pretest
        const std::vector<F3>& tr = mesh.tris;
        if (visit_triangles(mesh, ma, d, [&](uint32_t i) {
                float t = 0.0f;
                return segment_triangle_t(ma, d, tr[3 * i], tr[3 * i + 1], tr[3 * i + 2], 1.0f, t);
            }))
            return true;
    }
    return false;
}

}  // namespace alamo::tactical::movement
