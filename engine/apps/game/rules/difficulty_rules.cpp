// the DifficultyAdjustments row of a player (difficulty_rules.hpp).
#include "alamo/game/difficulty_rules.hpp"

#include <algorithm>

#include "alamo/galactic/galactic_rules.hpp"

namespace alamo::game {

int32_t clamp_difficulty(int32_t difficulty) { return std::clamp(difficulty, 0, 2); }

const char* difficulty_name(int32_t difficulty) { return kDifficultyNames[static_cast<size_t>(clamp_difficulty(difficulty))]; }

uint32_t difficulty_row(const galactic::GalacticRules& rules, const std::array<std::string, 3>* type_rows, int32_t difficulty) {
    if (!type_rows) return rules.find_difficulty(difficulty_name(difficulty));
    const std::string& name = (*type_rows)[static_cast<size_t>(clamp_difficulty(difficulty))];
    return name.empty() ? galactic::kNoIndex : rules.find_difficulty(name);
}

}  // namespace alamo::game
