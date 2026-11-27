// [port] Fleet groups (docs/DECISIONS.md "Fleet groups: exception to the current focus", Gabriel 2026-11-13): persistent,
// player-made unit groups in tactical battles. Not retail: every fleet group order is one PortExtension event (id 63) of
// subtype kFleetGroupSubtype, so the original event catalogue is unchanged.
//
// Payload (`data` bytes): op (u8), then per op
//   Create       nothing          the issuer's selection becomes a new group (its units leave their old groups)
//   Dissolve     nothing          every group with a unit in the selection is removed
//   RemoveUnits  nothing          the selected units leave their groups
//   SetLock      locked (u8 0/1)  every group with a unit in the selection is locked / unlocked
//   Rename       group (u16 LE), name length (u8), name bytes (UTF-8; over kFleetGroupNameMax the parser refuses it, #393)
//   Merge        nothing          every group with a unit in the selection folds into the oldest of them (#177)
//   AssignReserve group (u16 LE), reserve (u32 LE)  #645: the reserve (a Reinforce "unit") joins `group` when it arrives;
//                group 0 = no group, kAssignDefault = back to the galactic-fleet rule (#173)
// The sim applies them at the command phase from its synced selection (sim/fleet_groups.hpp).
#pragma once
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>

#include "alamo/commands/command.hpp"

namespace alamo::commands {

inline constexpr uint16_t kFleetGroupSubtype = 0x4647;  // "FG"
// limit: guard [port] fleet group name bytes (port-only feature), refuses
inline constexpr size_t kFleetGroupNameMax = 32;         // bytes; #393: a longer name is refused, never cut

enum class FleetGroupOp : uint8_t { Create = 0, Dissolve = 1, RemoveUnits = 2, SetLock = 3, Rename = 4, Merge = 5, AssignReserve = 6 };
inline constexpr uint16_t kAssignNoGroup = 0;        // #645 AssignReserve: the reserve stays ungrouped
inline constexpr uint16_t kAssignDefault = 0xFFFFu;  // #645 AssignReserve: the galactic-fleet rule (the assignment cleared)

struct FleetGroupCommand {
    FleetGroupOp op = FleetGroupOp::Create;
    bool locked = false;  // SetLock
    uint16_t group = 0;   // Rename, AssignReserve
    uint32_t reserve = 0; // AssignReserve: the reserve's Reinforce "unit" (its galactic object id)
    std::string name;     // Rename (encoded whole - at most 255 bytes fit the length byte; over kFleetGroupNameMax refused)
};

// The PortExtension event of `c`.
EventData fleet_group_event(const FleetGroupCommand& c);
// The command in a kFleetGroupSubtype payload; nullopt for an unknown op, a short / long payload or an over-long name.
std::optional<FleetGroupCommand> parse_fleet_group(std::span<const uint8_t> data);

}  // namespace alamo::commands
