#include "alamo/tactical/abilities/unit_ability_desc.hpp"

#include "alamo/tactical/combat/tables.hpp"

namespace alamo::tactical::abilities {

float ability_product(const UnitAbilityDesc* const slots[2], const bool active[2], combat::ModKind kind) {
    const auto k = static_cast<uint32_t>(kind) - 1u;
    float p = 1.0f;
    for (int s = 0; s < 2; ++s) {
        const UnitAbilityDesc* a = slots[s];
        if (!a || !active[s] || a->ability_type == combat::kAbilityLuckyShot || k >= combat::kModKindCount) continue;
        p = p * a->mult[k];
    }
    return p;
}

bool ability_flag(const UnitAbilityDesc* const slots[2], const bool active[2], uint8_t flag) {
    if (flag >= combat::kModFlagCount) return true;
    bool r = true;
    for (int s = 0; s < 2; ++s) {
        const UnitAbilityDesc* a = slots[s];
        if (!a || !active[s] || a->flag[flag] < 0) continue;
        r = r && a->flag[flag] != 0;
    }
    return r;
}

combat::UnitAbilityState unit_ability_state(const UnitAbilityDesc* const slots[2], const std::array<uint8_t, 2>& active) {
    combat::UnitAbilityState st;
    const bool act[2] = {active[0] != 0, active[1] != 0};
    for (size_t s = 0; s < 2; ++s) {
        st.present[s] = slots[s] ? uint8_t{1} : uint8_t{0};
        st.type[s] = slots[s] ? slots[s]->ability_type : uint8_t{0};
        st.active[s] = active[s];
    }
    for (uint32_t k = 0; k < combat::kModKindCount; ++k) st.mult[k] = ability_product(slots, act, static_cast<combat::ModKind>(k + 1));
    for (uint32_t f = 0; f < combat::kModFlagCount; ++f)
        st.flag[f] = ability_flag(slots, act, static_cast<uint8_t>(f)) ? uint8_t{1} : uint8_t{0};
    for (int s = 1; s >= 0; --s)  // active ability armor override, slot 1 before slot 0 [M]
        if (slots[s] && act[s] && slots[s]->armor_override != combat::kNoTypeId) {
            st.armor_override = slots[s]->armor_override;
            break;
        }
    for (int s = 0; s < 2; ++s)
        if (slots[s] && act[s] && slots[s]->range_override > 0.0f) {
            st.range_override = slots[s]->range_override;
            break;
        }
    for (int s = 0; s < 2; ++s)
        if (slots[s] && act[s] && slots[s]->inaccuracy_override > 0.0f) {
            st.inaccuracy_override = slots[s]->inaccuracy_override;
            break;
        }
    return st;
}

}  // namespace alamo::tactical::abilities
