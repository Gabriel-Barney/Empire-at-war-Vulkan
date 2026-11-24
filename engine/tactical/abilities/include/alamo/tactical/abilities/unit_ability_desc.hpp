#pragma once
// #417 (#150 ticket 2): the abilities module's Unit_Abilities_Data combat descriptor - Mod_Multiplier (system B), Mod_Flag,
// the Armor_Type / range / inaccuracy overrides - and the per-unit state it hands to combat (combat::UnitAbilityState:
// what combat reads of a unit's first two slots with its activity). Combat declares only the state; it never sees the
// descriptor.
#include <array>
#include <cstdint>

#include "alamo/tactical/combat/modifiers.hpp"

namespace alamo::tactical::abilities {

// One Unit_Abilities_Data record's combat part (Mod_Multiplier defaults 1.0, Mod_Flag "unset").
struct UnitAbilityDesc {
    uint8_t ability_type = 0;
    std::array<float, combat::kModKindCount> mult{1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f};
    std::array<int8_t, combat::kModFlagCount> flag{-1, -1, -1, -1};  // -1 = not given (does not take part in the AND)
    uint16_t armor_override = 0xFFFF;  // ability data +0x168 (0xFFFF = none)
    float range_override = 0.0f;       // ability data +0x108 (> 0 = override)
    float inaccuracy_override = 0.0f;  // ability data +0x1b4 (> 0 = override, used UNSCALED)
};

// Get_Unit_Ability_Mod_Multiplier (0x398010) for one object: product over the 2 slots where the
// ability is active, LUCKY_SHOT skipped. `slots` = the type's slot data (nullptr = empty slot).
float ability_product(const UnitAbilityDesc* const slots[2], const bool active[2], combat::ModKind kind);
// Get_Unit_Ability_Mod_Flag (0x397f60): AND across active slots whose ability sets the flag; true default.
bool ability_flag(const UnitAbilityDesc* const slots[2], const bool active[2], uint8_t flag);
// The state combat reads for a unit whose type's first two Unit_Abilities_Data slots are `slots` (nullptr = empty) and
// whose slot activity is `active`: per slot present / type / activity, the products and flags above, the active armor
// override (slot 1 before slot 0), the first active range / inaccuracy override.
combat::UnitAbilityState unit_ability_state(const UnitAbilityDesc* const slots[2], const std::array<uint8_t, 2>& active);

}  // namespace alamo::tactical::abilities
