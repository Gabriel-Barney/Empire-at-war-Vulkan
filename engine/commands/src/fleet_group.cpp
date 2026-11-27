#include "alamo/commands/fleet_group.hpp"

#include <vector>

namespace alamo::commands {

EventData fleet_group_event(const FleetGroupCommand& c) {
    std::vector<uint8_t> b;
    b.push_back(static_cast<uint8_t>(c.op));
    switch (c.op) {
        case FleetGroupOp::SetLock: b.push_back(c.locked ? 1u : 0u); break;
        case FleetGroupOp::Rename: {
            b.push_back(static_cast<uint8_t>(c.group & 0xFFu));
            b.push_back(static_cast<uint8_t>(c.group >> 8));
            // #393 (docs/MODS.md "Limits policy"): the name goes out whole, so the guard in parse_fleet_group refuses an
            // over-long one instead of a silent cut here. Only the u8 length field bounds it (a name of 256+ bytes is sent as
            // its first 255, which the guard refuses all the same).
            const size_t n = c.name.size() < 255u ? c.name.size() : 255u;
            b.push_back(static_cast<uint8_t>(n));
            for (size_t i = 0; i < n; ++i) b.push_back(static_cast<uint8_t>(c.name[i]));
            break;
        }
        case FleetGroupOp::AssignReserve:
            b.push_back(static_cast<uint8_t>(c.group & 0xFFu));
            b.push_back(static_cast<uint8_t>(c.group >> 8));
            for (int i = 0; i < 4; ++i) b.push_back(static_cast<uint8_t>(c.reserve >> (8 * i)));
            break;
        case FleetGroupOp::Create:
        case FleetGroupOp::Dissolve:
        case FleetGroupOp::RemoveUnits:
        case FleetGroupOp::Merge: break;
    }
    return ev::port_extension(kFleetGroupSubtype, std::span<const uint8_t>(b));
}

std::optional<FleetGroupCommand> parse_fleet_group(std::span<const uint8_t> data) {
    if (data.empty()) return std::nullopt;
    FleetGroupCommand c;
    switch (data[0]) {
        case static_cast<uint8_t>(FleetGroupOp::Create):
        case static_cast<uint8_t>(FleetGroupOp::Dissolve):
        case static_cast<uint8_t>(FleetGroupOp::RemoveUnits):
        case static_cast<uint8_t>(FleetGroupOp::Merge):
            if (data.size() != 1) return std::nullopt;
            c.op = static_cast<FleetGroupOp>(data[0]);
            return c;
        case static_cast<uint8_t>(FleetGroupOp::SetLock):
            if (data.size() != 2 || data[1] > 1) return std::nullopt;
            c.op = FleetGroupOp::SetLock;
            c.locked = data[1] != 0;
            return c;
        case static_cast<uint8_t>(FleetGroupOp::Rename): {
            if (data.size() < 4) return std::nullopt;
            const size_t n = data[3];
            if (n > kFleetGroupNameMax || data.size() != 4 + n) return std::nullopt;
            c.op = FleetGroupOp::Rename;
            c.group = static_cast<uint16_t>(data[1] | (data[2] << 8));
            c.name.assign(reinterpret_cast<const char*>(data.data() + 4), n);
            return c;
        }
        case static_cast<uint8_t>(FleetGroupOp::AssignReserve):
            if (data.size() != 7) return std::nullopt;
            c.op = FleetGroupOp::AssignReserve;
            c.group = static_cast<uint16_t>(data[1] | (data[2] << 8));
            c.reserve = static_cast<uint32_t>(data[3]) | (static_cast<uint32_t>(data[4]) << 8) | (static_cast<uint32_t>(data[5]) << 16) |
                        (static_cast<uint32_t>(data[6]) << 24);
            return c;
        default: return std::nullopt;
    }
}

}  // namespace alamo::commands
