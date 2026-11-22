// The DifficultyAdjustments row a player plays at, shared by alamo_game (session_spec.hpp, game_session.cpp) and
// alamo_headless (ai_run.cpp, save_run.cpp). Data in, a row out: no session, sim or AI code.
//   difficulty_name    difficulty 0..2 -> the fixed DifficultyAdjustments row name; the one table of those names.
//   difficulty_row     a player's row: its AIPlayerType's Difficulty_Adjustments entry, the fixed name without an AI type.
#pragma once
#include <array>
#include <cstdint>
#include <string>

namespace alamo::galactic {
struct GalacticRules;
}

namespace alamo::game {

// Difficulty 0..2 -> DifficultyAdjustments name ([port] Easy_Default / Normal_Default / Hard_Default); out of range clamps.
inline constexpr std::array<const char*, 3> kDifficultyNames = {"Easy_Default", "Normal_Default", "Hard_Default"};
const char* difficulty_name(int32_t difficulty);
int32_t clamp_difficulty(int32_t difficulty);  // 0..2

// gai1 t06 (legacy docs/re/AI_GALACTIC.md §1.2, GetAdj 0x4b0500 [H]): the DifficultyAdjustments row (an index of
// rules.difficulties) of a player at `difficulty` 0..2 (clamped), the row economy and combat read (PlayerState::difficulty).
//   type_rows set     the player's AIPlayerType Difficulty_Adjustments Easy / Normal / Hard names (AiPlayerTypeDef::
//                     difficulty): that entry's row, as the galactic and tactical AIs read it. An empty entry or one that
//                     names no row is kNoIndex: retail's all-1.0 dummy.
//   type_rows null    no AIPlayerType (a human, a player the data gives no AI): [port] the fixed name's row
//                     (difficulty_name), kNoIndex when the data has none.
// Shipped FoC data maps every AIPlayerType to Easy / Normal / Hard_Default, so both give the same row there.
uint32_t difficulty_row(const galactic::GalacticRules& rules, const std::array<std::string, 3>* type_rows, int32_t difficulty);

}  // namespace alamo::game
