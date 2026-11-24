// #143: the task-force group order encoder and its guard (docs/MODS.md "Limits policy"), kept apart from tactical_ai.hpp so
// the encoder is testable without the Lua host.
#pragma once
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

#include "alamo/commands/command.hpp"

namespace alamo::ai::tactical {

// limit: guard task-force group order ids (payload < 16 KiB), refuses
inline constexpr size_t kTfGroupOrderMaxUnits = 4096;
// The L1 object-order event (object_bridge.hpp: PortExtension, `subtype` = kLuaOrderSubtype + order) for a task force's
// movable units as ONE group order: the 24-byte block (units[0], target, x, y, z, arg), then one u32 per further unit.
// #143 (docs/MODS.md "Limits policy"): nullopt past kTfGroupOrderMaxUnits units - the guard refuses, it never cuts.
std::optional<commands::EventData> tf_group_order_event(uint32_t subtype, std::span<const uint32_t> units, int32_t target, float x,
                                                        float y, float z, uint32_t arg);

}  // namespace alamo::ai::tactical
