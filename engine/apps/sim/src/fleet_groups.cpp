// [port] Fleet groups, phase 1: sim/fleet_groups.hpp.
#include "alamo/sim/fleet_groups.hpp"

#include <algorithm>

#include "alamo/core/bytes.hpp"
#include "alamo/core/sim_crc.hpp"

namespace alamo::sim {

namespace mv = tactical::movement;
using R = commands::RejectReason;

namespace {

constexpr uint32_t kSaveVersion = 3;  // 3: + the reserve assignments (#645); 2: FleetGroup::fleet (#173); 1 and 2 still load

std::vector<objects::ObjectId> sorted_unique(std::span<const objects::ObjectId> units) {
    std::vector<objects::ObjectId> v(units.begin(), units.end());
    std::sort(v.begin(), v.end());
    v.erase(std::unique(v.begin(), v.end()), v.end());
    return v;
}

bool contains(const std::vector<objects::ObjectId>& sorted, objects::ObjectId id) {
    return std::binary_search(sorted.begin(), sorted.end(), id);
}

bool has_any(const FleetGroup& g, const std::vector<objects::ObjectId>& sorted) {
    for (const FleetMember& m : g.members)
        if (contains(sorted, m.id)) return true;
    return false;
}

bool explicit_facing(float facing_deg) { return facing_deg - facing_deg == 0.0f && facing_deg < 1.0e17f && facing_deg > -1.0e17f; }

}  // namespace

const FleetGroup* FleetGroups::group_of(objects::ObjectId id) const {
    for (const FleetGroup& g : groups_)
        for (const FleetMember& m : g.members)
            if (m.id == id) return &g;
    return nullptr;
}

const FleetGroup* FleetGroups::group(uint16_t id) const {
    for (const FleetGroup& g : groups_)
        if (g.id == id) return &g;
    return nullptr;
}

FleetGroup* FleetGroups::find(uint16_t id) {
    for (FleetGroup& g : groups_)
        if (g.id == id) return &g;
    return nullptr;
}

bool FleetGroups::any_moving() const {
    for (const FleetGroup& g : groups_)
        if (g.moving) return true;
    return false;
}

void FleetGroups::remove_units(std::span<const objects::ObjectId> units) {
    const std::vector<objects::ObjectId> s = sorted_unique(units);
    for (FleetGroup& g : groups_)
        g.members.erase(std::remove_if(g.members.begin(), g.members.end(), [&s](const FleetMember& m) { return contains(s, m.id); }),
                        g.members.end());
}

void FleetGroups::drop_empty() {
    groups_.erase(std::remove_if(groups_.begin(), groups_.end(), [](const FleetGroup& g) { return g.members.empty(); }), groups_.end());
}

// Slots from the members' current positions: the centre is their mean (summed in member order), the frame's forward the
// first valid member's yaw.
void FleetGroups::capture_slots(FleetGroup& g, const InfoFn& info) {
    mv::V2 sum{0.0f, 0.0f};
    uint32_t n = 0;
    bool have_facing = false;
    std::vector<FleetUnitInfo> infos;
    infos.reserve(g.members.size());
    for (const FleetMember& m : g.members) {
        infos.push_back(info(m.id));
        const FleetUnitInfo& u = infos.back();
        if (!u.valid) continue;
        sum = sum + u.pos;
        ++n;
        if (!have_facing) {
            g.facing_deg = u.yaw_deg;
            have_facing = true;
        }
    }
    if (n == 0) return;
    const mv::V2 centre = sum * (1.0f / static_cast<float>(n));
    const mv::V2 f = mv::dir_from_deg(g.facing_deg);
    const mv::V2 l = mv::perp_left(f);
    for (size_t i = 0; i < g.members.size(); ++i) {
        FleetMember& m = g.members[i];
        m.moving = false;
        if (!infos[i].valid) continue;
        const mv::V2 d = infos[i].pos - centre;
        m.slot = {mv::dot(d, f), mv::dot(d, l)};
    }
    g.moving = false;
}

void FleetGroups::join_spawned(uint8_t player, uint32_t fleet, std::span<const objects::ObjectId> units, const InfoFn& info) {
    if (fleet == FleetGroup::kNoFleet) return;
    for (const objects::ObjectId id : sorted_unique(units)) {
        const FleetUnitInfo u = info(id);
        if (!u.valid || u.owner != static_cast<int32_t>(player) || group_of(id)) continue;
        FleetGroup* g = nullptr;
        size_t owned = 0;
        for (FleetGroup& x : groups_) {
            if (x.owner != player) continue;
            ++owned;
            if (!g && x.fleet == fleet && x.members.size() < kMaxMembers) g = &x;
        }
        if (!g) {
            if (owned >= kMaxGroupsPerPlayer || next_id_ == 0xFFFFu) continue;
            FleetGroup made;
            made.id = next_id_++;
            made.owner = player;
            made.fleet = fleet;
            made.facing_deg = u.yaw_deg;
            groups_.push_back(std::move(made));
            g = &groups_.back();
            used_ = true;
        }
        add_joiner(*g, id, u, info);
    }
}

void FleetGroups::add_joiner(FleetGroup& g, objects::ObjectId id, const FleetUnitInfo& u, const InfoFn& info) {
    // The slot in the members' frame: the offset from their current centre plus their mean slot, so the joiner
    // sits where the frame the slots were captured in puts it (plan_move measures slots from their mean).
    mv::V2 pos_sum{0.0f, 0.0f}, slot_sum{0.0f, 0.0f};
    uint32_t count = 0;
    for (const FleetMember& m : g.members) {
        const FleetUnitInfo mu = info(m.id);
        if (!mu.valid) continue;
        pos_sum = pos_sum + mu.pos;
        slot_sum = slot_sum + m.slot;
        ++count;
    }
    FleetMember m;
    m.id = id;
    if (count > 0) {
        const float k = 1.0f / static_cast<float>(count);
        const mv::V2 d = u.pos - pos_sum * k;
        const mv::V2 f = mv::dir_from_deg(g.facing_deg);
        m.slot = mv::V2{mv::dot(d, f), mv::dot(d, mv::perp_left(f))} + slot_sum * k;
    }
    const auto at = std::lower_bound(g.members.begin(), g.members.end(), id,
                                     [](const FleetMember& x, objects::ObjectId v) { return x.id < v; });
    g.members.insert(at, m);
}

R FleetGroups::assign_reserve(uint8_t player, uint32_t reserve, uint16_t group) {
    if (group != commands::kAssignNoGroup && group != commands::kAssignDefault) {
        const FleetGroup* g = this->group(group);
        if (!g || g->owner != player) return R::InvalidTarget;
    }
    const auto at = std::lower_bound(assigned_.begin(), assigned_.end(), reserve,
                                     [](const ReserveAssignment& a, uint32_t r) { return a.reserve < r; });
    const bool found = at != assigned_.end() && at->reserve == reserve;
    if (group == commands::kAssignDefault) {
        if (found) assigned_.erase(at);
        return R::None;
    }
    if (found) {
        at->owner = player;
        at->group = group;
    } else {
        assigned_.insert(at, ReserveAssignment{reserve, player, group});
    }
    used_ = true;  // saved and hashed from now on
    return R::None;
}

uint16_t FleetGroups::reserve_assignment(uint32_t reserve) const {
    const auto at = std::lower_bound(assigned_.begin(), assigned_.end(), reserve,
                                     [](const ReserveAssignment& a, uint32_t r) { return a.reserve < r; });
    return at != assigned_.end() && at->reserve == reserve ? at->group : commands::kAssignDefault;
}

void FleetGroups::join_reserve(uint8_t player, uint32_t reserve, uint32_t fleet, std::span<const objects::ObjectId> units,
                               const InfoFn& info) {
    const auto at = std::lower_bound(assigned_.begin(), assigned_.end(), reserve,
                                     [](const ReserveAssignment& a, uint32_t r) { return a.reserve < r; });
    if (at == assigned_.end() || at->reserve != reserve || at->owner != player) {
        join_spawned(player, fleet, units, info);
        return;
    }
    const uint16_t pick = at->group;
    assigned_.erase(at);
    if (pick == commands::kAssignNoGroup) return;
    FleetGroup* g = find(pick);
    if (!g || g->owner != player) {  // gone since the pick: the galactic-fleet rule
        join_spawned(player, fleet, units, info);
        return;
    }
    std::vector<objects::ObjectId> rest;
    for (const objects::ObjectId id : sorted_unique(units)) {
        const FleetUnitInfo u = info(id);
        if (!u.valid || u.owner != static_cast<int32_t>(player) || group_of(id)) continue;
        if (g->members.size() < kMaxMembers) add_joiner(*g, id, u, info);
        else rest.push_back(id);  // full: the rest take the galactic-fleet rule
    }
    if (!rest.empty()) join_spawned(player, fleet, rest, info);
}

R FleetGroups::apply(uint8_t player, const commands::FleetGroupCommand& c, std::span<const objects::ObjectId> units,
                     const InfoFn& info) {
    using Op = commands::FleetGroupOp;
    const std::vector<objects::ObjectId> sel = sorted_unique(units);
    switch (c.op) {
        case Op::Create: {
            if (sel.empty()) return R::InvalidUnit;
            if (sel.size() > kMaxMembers) return R::QueueFull;  // #143: the member guard refuses (as Merge), never cuts
            // Groups the selection empties make room; count the player's groups that remain.
            size_t remaining = 0;
            for (const FleetGroup& g : groups_) {
                if (g.owner != player) continue;
                bool emptied = true;
                for (const FleetMember& m : g.members)
                    if (!contains(sel, m.id)) emptied = false;
                if (!emptied) ++remaining;
            }
            if (remaining >= kMaxGroupsPerPlayer || next_id_ == 0xFFFFu) return R::QueueFull;
            remove_units(sel);
            drop_empty();
            FleetGroup g;
            g.id = next_id_++;
            g.owner = player;
            for (const objects::ObjectId id : sel) {
                FleetMember m;
                m.id = id;
                g.members.push_back(m);
            }
            capture_slots(g, info);
            groups_.push_back(std::move(g));
            used_ = true;
            return R::None;
        }
        case Op::Dissolve: {
            const size_t before = groups_.size();
            groups_.erase(std::remove_if(groups_.begin(), groups_.end(),
                                         [&](const FleetGroup& g) { return g.owner == player && has_any(g, sel); }),
                          groups_.end());
            return groups_.size() == before ? R::InvalidUnit : R::None;
        }
        case Op::RemoveUnits: {
            bool any = false;
            for (const FleetGroup& g : groups_)
                if (g.owner == player && has_any(g, sel)) any = true;
            if (!any) return R::InvalidUnit;
            remove_units(sel);
            drop_empty();
            return R::None;
        }
        case Op::SetLock: {
            bool any = false;
            for (FleetGroup& g : groups_) {
                if (g.owner != player || !has_any(g, sel)) continue;
                any = true;
                if (c.locked && !g.locked) capture_slots(g, info);
                if (!c.locked) {
                    g.moving = false;
                    for (FleetMember& m : g.members) m.moving = false;
                }
                g.locked = c.locked;
            }
            return any ? R::None : R::InvalidUnit;
        }
        case Op::Merge: {  // #177: into the oldest group with a selected unit (groups_ is in creation = id order)
            std::vector<size_t> hit;
            size_t total = 0;
            for (size_t i = 0; i < groups_.size(); ++i)
                if (groups_[i].owner == player && has_any(groups_[i], sel)) {
                    hit.push_back(i);
                    total += groups_[i].members.size();
                }
            if (hit.size() < 2) return R::InvalidUnit;
            if (total > kMaxMembers) return R::QueueFull;
            FleetGroup& keep = groups_[hit[0]];
            for (size_t k = 1; k < hit.size(); ++k)
                for (const FleetMember& m : groups_[hit[k]].members) keep.members.push_back(m);
            std::sort(keep.members.begin(), keep.members.end(),
                      [](const FleetMember& x, const FleetMember& y) { return x.id < y.id; });
            keep.moving = false;
            for (FleetMember& m : keep.members) m.moving = false;
            capture_slots(keep, info);
            for (size_t k = hit.size(); k-- > 1;) groups_.erase(groups_.begin() + static_cast<std::ptrdiff_t>(hit[k]));
            return R::None;
        }
        case Op::Rename: {
            FleetGroup* g = find(c.group);
            if (!g || g->owner != player) return R::InvalidTarget;
            if (c.name.size() > commands::kFleetGroupNameMax) return R::Malformed;  // #393: refused, never cut
            g->name = c.name;
            return R::None;
        }
        case Op::AssignReserve: return R::Malformed;  // #645: TacticalBattle checks the reserve, then assign_reserve
    }
    return R::Malformed;
}

void FleetGroups::plan_move(uint8_t player, std::span<const objects::ObjectId> units, mv::V2 dest, float facing_deg, const InfoFn& info,
                            std::vector<LockedMove>& moves, std::vector<objects::ObjectId>& rest) const {
    moves.clear();
    rest.clear();
    const std::vector<objects::ObjectId> sel = sorted_unique(units);
    std::vector<objects::ObjectId> taken;
    for (const FleetGroup& g : groups_) {
        if (g.owner != player || !g.locked) continue;
        mv::V2 pos_sum{0.0f, 0.0f}, slot_sum{0.0f, 0.0f};
        uint32_t n = 0;
        std::vector<const FleetMember*> movers;
        for (const FleetMember& m : g.members) {
            if (!contains(sel, m.id)) continue;
            const FleetUnitInfo u = info(m.id);
            if (!u.valid) continue;
            movers.push_back(&m);
            pos_sum = pos_sum + u.pos;
            slot_sum = slot_sum + m.slot;
            ++n;
        }
        if (n == 0) continue;
        const float inv = 1.0f / static_cast<float>(n);
        const mv::V2 centre = pos_sum * inv, mean_slot = slot_sum * inv;
        LockedMove lm;
        lm.group = g.id;
        if (explicit_facing(facing_deg)) lm.facing_deg = facing_deg;
        else if (mv::length_sq(dest - centre) > 1.0f) lm.facing_deg = mv::heading_deg(dest - centre);
        else lm.facing_deg = g.facing_deg;
        const mv::V2 f = mv::dir_from_deg(lm.facing_deg);
        const mv::V2 l = mv::perp_left(f);
        for (const FleetMember* m : movers) {
            const mv::V2 s = m->slot - mean_slot;
            lm.goals.push_back({m->id, dest + f * s.x + l * s.y});
            taken.push_back(m->id);
        }
        moves.push_back(std::move(lm));
    }
    std::sort(taken.begin(), taken.end());
    for (const objects::ObjectId id : units)
        if (!contains(taken, id)) rest.push_back(id);
}

void FleetGroups::start_move(const LockedMove& lm, const InfoFn& info) {
    FleetGroup* g = find(lm.group);
    if (!g) return;
    g->moving = true;
    g->facing_deg = lm.facing_deg;
    for (FleetMember& m : g->members) {
        m.moving = false;
        for (const MemberGoal& goal : lm.goals) {
            if (goal.id != m.id) continue;
            const FleetUnitInfo u = info(m.id);
            m.moving = true;
            m.goal = goal.goal;
            m.start_dist = u.valid ? mv::distance(u.pos, goal.goal) : 0.0f;
        }
    }
}

void FleetGroups::interrupt(std::span<const objects::ObjectId> units) {
    const std::vector<objects::ObjectId> sel = sorted_unique(units);
    for (FleetGroup& g : groups_) {
        if (!g.moving || !has_any(g, sel)) continue;
        g.moving = false;
        for (FleetMember& m : g.members) m.moving = false;
    }
}

void FleetGroups::prune(const InfoFn& info) {
    bool changed = false;
    for (FleetGroup& g : groups_) {
        const size_t before = g.members.size();
        const int32_t owner = g.owner;
        g.members.erase(std::remove_if(g.members.begin(), g.members.end(),
                                       [&info, owner](const FleetMember& m) {
                                           const FleetUnitInfo u = info(m.id);
                                           return !u.valid || u.owner != owner;
                                       }),
                        g.members.end());
        changed = changed || g.members.size() != before;
    }
    if (changed) drop_empty();
}

void FleetGroups::regulate(const InfoFn& info, const std::function<void(objects::ObjectId, float)>& scale) {
    for (FleetGroup& g : groups_) {
        if (!g.moving) continue;
        std::vector<FleetUnitInfo> infos;
        infos.reserve(g.members.size());
        bool any_goal = false;
        float slowest = 3.0e38f, p_min = 3.0e38f;
        for (const FleetMember& m : g.members) {
            infos.push_back(m.moving ? info(m.id) : FleetUnitInfo{});
            const FleetUnitInfo& u = infos.back();
            if (!m.moving || !u.valid || !u.has_goal) continue;
            any_goal = true;
            if (u.max_speed > 0.0f && u.max_speed < slowest) slowest = u.max_speed;
            const float p = progress(m, u);
            if (p < p_min) p_min = p;
        }
        if (!any_goal) {
            g.moving = false;
            for (FleetMember& m : g.members) m.moving = false;
            continue;
        }
        for (size_t i = 0; i < g.members.size(); ++i) {
            const FleetMember& m = g.members[i];
            const FleetUnitInfo& u = infos[i];
            if (!m.moving || !u.valid || !u.has_goal || !(u.max_speed > 0.0f)) continue;
            float s = slowest < u.max_speed ? slowest / u.max_speed : 1.0f;
            const float p = progress(m, u);
            const float lead = (p - p_min) * m.start_dist;
            if (lead > straggler_distance && straggler_distance > 0.0f)
                s = s * std::max(min_leader_scale, 1.0f - (lead - straggler_distance) / straggler_distance);
            if (s < 1.0f) scale(m.id, s);
        }
    }
}

float FleetGroups::progress(const FleetMember& m, const FleetUnitInfo& u) {
    return m.start_dist > 1.0e-3f ? 1.0f - mv::distance(u.pos, m.goal) / m.start_dist : 1.0f;
}

std::vector<FleetGroups::Lagging> FleetGroups::lagging(const InfoFn& info) const {
    std::vector<Lagging> out;
    if (!(straggler_distance > 0.0f)) return out;
    struct Moving {
        objects::ObjectId id;
        float p, start_dist;
        bool leader;
    };
    std::vector<Moving> moving;
    for (const FleetGroup& g : groups_) {
        if (!g.moving) continue;
        moving.clear();
        for (const FleetMember& m : g.members) {
            if (!m.moving) continue;
            const FleetUnitInfo u = info(m.id);
            if (!u.valid || !u.has_goal) continue;
            moving.push_back({m.id, progress(m, u), m.start_dist, u.max_speed > 0.0f});
        }
        Lagging l;
        l.group = g.id;
        for (const Moving& s : moving)
            for (const Moving& lead : moving)
                if (lead.leader && lead.id != s.id && (lead.p - s.p) * lead.start_dist > straggler_distance) {
                    l.members.push_back(s.id);
                    break;
                }
        if (!l.members.empty()) out.push_back(std::move(l));
    }
    return out;
}

void FleetGroups::hash(StateHasher& h) const {
    h.u16(next_id_);
    if (!assigned_.empty()) {  // #645: only once a pick exists, so a battle without one hashes as before
        h.u32(static_cast<uint32_t>(assigned_.size()));
        for (const ReserveAssignment& a : assigned_) {
            h.u32(a.reserve);
            h.u8(a.owner);
            h.u16(a.group);
        }
    }
    h.u32(static_cast<uint32_t>(groups_.size()));
    for (const FleetGroup& g : groups_) {
        h.u16(g.id);
        h.u8(g.owner);
        h.u32(g.fleet);
        h.boolean(g.locked);
        h.boolean(g.moving);
        h.f32(g.facing_deg);
        h.str(g.name);
        h.u32(static_cast<uint32_t>(g.members.size()));
        for (const FleetMember& m : g.members) {
            h.i32(m.id);
            h.f32(m.slot.x);
            h.f32(m.slot.y);
            h.boolean(m.moving);
            if (!m.moving) continue;
            h.f32(m.goal.x);
            h.f32(m.goal.y);
            h.f32(m.start_dist);
        }
    }
}

void FleetGroups::write(ByteWriter& w) const {
    w.u32(kSaveVersion);
    w.u16(next_id_);
    w.boolean(used_);
    w.u32(static_cast<uint32_t>(groups_.size()));
    for (const FleetGroup& g : groups_) {
        w.u16(g.id).u8(g.owner).u32(g.fleet).boolean(g.locked).boolean(g.moving).f32(g.facing_deg).str(g.name);
        w.u32(static_cast<uint32_t>(g.members.size()));
        for (const FleetMember& m : g.members) {
            w.i32(m.id).f32(m.slot.x).f32(m.slot.y).f32(m.goal.x).f32(m.goal.y).f32(m.start_dist).boolean(m.moving);
        }
    }
    w.u32(static_cast<uint32_t>(assigned_.size()));  // #645 (v3)
    for (const ReserveAssignment& a : assigned_) w.u32(a.reserve).u8(a.owner).u16(a.group);
}

bool FleetGroups::read(ByteReader& r) {
    const uint32_t version = r.u32();
    if ((version != 1 && version != 2 && version != kSaveVersion) || !r.ok()) return false;
    const uint16_t next = r.u16();
    const bool used = r.boolean();
    uint32_t n = 0;
    if (!r.count(n, 12)) return false;
    std::vector<FleetGroup> groups;
    groups.reserve(n);
    for (uint32_t i = 0; i < n; ++i) {
        FleetGroup g;
        g.id = r.u16();
        g.owner = r.u8();
        if (version >= 2) g.fleet = r.u32();
        g.locked = r.boolean();
        g.moving = r.boolean();
        g.facing_deg = r.f32();
        g.name = r.str();
        uint32_t members = 0;
        if (!r.count(members, 25) || members == 0 || members > kMaxMembers || g.name.size() > commands::kFleetGroupNameMax ||
            g.id == 0 || g.id >= next)
            return false;
        for (uint32_t k = 0; k < members; ++k) {
            FleetMember m;
            m.id = r.i32();
            m.slot.x = r.f32();
            m.slot.y = r.f32();
            m.goal.x = r.f32();
            m.goal.y = r.f32();
            m.start_dist = r.f32();
            m.moving = r.boolean();
            if (!g.members.empty() && !(g.members.back().id < m.id)) return false;
            g.members.push_back(m);
        }
        groups.push_back(std::move(g));
    }
    std::vector<ReserveAssignment> assigned;
    if (version >= 3) {
        uint32_t na = 0;
        if (!r.count(na, 7)) return false;
        for (uint32_t k = 0; k < na; ++k) {
            ReserveAssignment a;
            a.reserve = r.u32();
            a.owner = r.u8();
            a.group = r.u16();
            if (!r.ok() || a.group == commands::kAssignDefault || (!assigned.empty() && !(assigned.back().reserve < a.reserve)))
                return false;
            assigned.push_back(a);
        }
    }
    if (!r.ok()) return false;
    groups_ = std::move(groups);
    assigned_ = std::move(assigned);
    next_id_ = next;
    used_ = used;
    return true;
}

}  // namespace alamo::sim
