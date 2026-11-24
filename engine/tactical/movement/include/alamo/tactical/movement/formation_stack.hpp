#pragma once
// #488 (#438, FIGHTER_FLYING §4.11 B, MOVEMENT §6.1 / §6.2 [H]): the move-block stack rules of a formation (FormationClass
// blocks F +0xf8..+0x100), shared by the space fighter formations (MovementSystem, fighter_formation.cpp) and the land
// formation records (TacticalBattle::LandFormationRecord). blocks.back() is the TOP (the current block, blocks[n-1]),
// blocks[0] the bottom (the order's block). The functions only edit the block vector; the formation's state, recorded
// goal and re-plan stay with the caller, which knows its units.
#include <cstdint>
#include <functional>
#include <optional>
#include <vector>

#include "alamo/tactical/movement/fighter.hpp"

namespace alamo::tactical::movement {

// The goal point of a block (a position block: its point; an object block: the object's position), false when unknown.
using BlockGoalFn = std::function<bool(const FighterBlock& b, V2& out)>;

// The block equality 0x40dc90: kind, flags, the push class, the bytes, and the kind's own fields.
bool same_block(const FighterBlock& a, const FighterBlock& b);

// Set_Move 0x423c10(F, b, clear): `clear` empties the stack; an empty stack gets b as its only block; b equal to blocks[0]
// changes nothing (false); else blocks[0] := b. True when the stack changed (the caller re-plans: speed -1, state 0).
bool stack_set_move(std::vector<FighterBlock>& blocks, const FighterBlock& b, bool clear);

// Pop 0x4105a0, n > 1 only (else nullopt): the top T goes; a patrol T (+0x5e) gets 0x08 (queued) and is re-inserted at the
// bottom. Returns T (the caller: recorded goal := goal(new top); state 5 after an object block, else 0).
std::optional<FighterBlock> stack_pop(std::vector<FighterBlock>& blocks);

// Push 0x41fd30's pre-pop and push: while n > 1 the top goes (stack_pop) unless it is a guard, a patrol block (+0x5e) or of
// another push class than b; then b goes on top.
void stack_push(std::vector<FighterBlock>& blocks, const FighterBlock& b);

// The waypoint append 0x420f70. Refused at `max_queued` queued (0x08) blocks; b gets 0x04 (player order); an empty stack
// gets b as its only block; b equal to blocks[0] is ignored; an earlier player waypoint (0x04, index 1..n-1) whose goal is
// within `loop_radius` (land 20, space 80) of b's closes a patrol loop (blocks[0..i] +0x5e := 1, b not added); else b goes
// to the BOTTOM (it runs after everything queued).
enum class AppendResult : uint8_t { Refused, Ignored, Added, Loop };
AppendResult stack_append(std::vector<FighterBlock>& blocks, FighterBlock b, uint32_t max_queued, float loop_radius,
                          const BlockGoalFn& goal);

}  // namespace alamo::tactical::movement
