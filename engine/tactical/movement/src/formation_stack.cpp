#include "alamo/tactical/movement/formation_stack.hpp"

#include <cmath>

#include "alamo/tactical/movement/movement_system.hpp"  // kMoveFlag*

namespace alamo::tactical::movement {

bool same_block(const FighterBlock& a, const FighterBlock& b) {
    if (a.kind != b.kind || a.flags != b.flags || a.pushed != b.pushed || a.own_speed != b.own_speed || a.loop != b.loop)
        return false;
    if (a.kind == kFighterBlockObject) return a.object == b.object && a.hard_point == b.hard_point;
    return a.point.x == b.point.x && a.point.y == b.point.y && a.facing == b.facing;
}

bool stack_set_move(std::vector<FighterBlock>& blocks, const FighterBlock& b, bool clear) {
    if (clear) blocks.clear();
    if (blocks.empty()) {
        blocks.push_back(b);
        return true;
    }
    if (same_block(b, blocks[0])) return false;
    blocks[0] = b;
    return true;
}

std::optional<FighterBlock> stack_pop(std::vector<FighterBlock>& blocks) {
    if (blocks.size() <= 1) return std::nullopt;
    FighterBlock t = blocks.back();
    blocks.pop_back();
    if (t.loop) {
        FighterBlock again = t;
        again.flags = static_cast<uint8_t>(again.flags | kMoveFlagQueue);
        blocks.insert(blocks.begin(), again);
    }
    return t;
}

void stack_push(std::vector<FighterBlock>& blocks, const FighterBlock& b) {
    while (blocks.size() > 1) {
        const FighterBlock& t = blocks.back();
        if ((t.flags & kMoveFlagGuardMove) != 0 || t.loop || t.pushed != b.pushed) break;
        (void)stack_pop(blocks);
    }
    blocks.push_back(b);
}

AppendResult stack_append(std::vector<FighterBlock>& blocks, FighterBlock b, uint32_t max_queued, float loop_radius,
                          const BlockGoalFn& goal) {
    uint32_t queued = 0;
    for (const FighterBlock& x : blocks) queued += (x.flags & kMoveFlagQueue) != 0 ? 1u : 0u;
    if (queued >= max_queued) return AppendResult::Refused;
    b.flags = static_cast<uint8_t>(b.flags | kMoveFlagPlayerOrder);
    const size_t n = blocks.size();
    if (n == 0) {
        blocks.push_back(b);
        return AppendResult::Added;
    }
    if (same_block(b, blocks[0])) return AppendResult::Ignored;
    V2 gb;
    const bool have_gb = goal && goal(b, gb);
    for (size_t i = 1; i < n && have_gb; ++i) {
        V2 gi;
        if ((blocks[i].flags & kMoveFlagPlayerOrder) == 0 || !goal(blocks[i], gi)) continue;
        if (std::sqrt(length_sq(gi - gb)) > loop_radius) continue;
        for (size_t k = 0; k <= i; ++k) blocks[k].loop = true;
        return AppendResult::Loop;
    }
    blocks.insert(blocks.begin(), b);
    return AppendResult::Added;
}

}  // namespace alamo::tactical::movement
