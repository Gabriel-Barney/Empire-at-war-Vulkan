# Set_Difficulty and hard-point health (#434)

Retail FoC x64 `StarWarsG.exe` (v1.121.859328), read in the Ghidra export (reference only, not copied) on 2026-11-24.
Tiers: **[H]** read directly in the code, **[M]** inferred from the code with a gap named, **[L]** a guess.
Answers #434 (old gai5/t05, b0491): does a hard point's maximum health carry the AI Health_Multiplier, and what does
Set_Difficulty (0x4b0dc0, legacy docs/re/AI_GALACTIC.md §1.2) do to hard points?

## The hard point's maximum health 0x385ae0 [H]

The maximum is computed when it is read, not stored. It is the hard-point type's base health, times the owner's
difficulty Health_Multiplier (difficulty record +0x50, through 0x4b0500) when the owner player has an AI system
(player +0x360), times (1 + the sum of a hard-point health modifier) when the owning object carries one. A player
without an AI system (a human) gets factor 1.

So a hard point of an AI player spawns, and stays, with max = base × Health_Multiplier(the player's current
difficulty).

## Set_Difficulty 0x4b0dc0 [H]

With old = the previous difficulty's multipliers (Health +0x50, Shield +0x54; 1.0 when the AI system had no difficulty
yet, +0x58 = -1):

1. It stores the new difficulty first (AI system +0x58).
2. For each object of the player (the player's object list):
   - hull: set to the object's hull value (+0x5C) × new Health / old Health;
   - each hard point whose current health (+0x28) is > 0: set to current × new Health / old Health, through the hard
     point's set-health 0x387ca0. That setter clamps to [0, max] with max read by 0x385ae0, which already uses the
     new difficulty, and it does nothing for a hard-point type that cannot take damage (type +0x4D clear);
   - shield: set to the current shield × new Shield / old Shield.

A destroyed hard point (health 0) stays at 0.

## Port consequence

- `CombatSystem::hp_max` takes the owner's AI Health_Multiplier, as hull and shield maxima already do.
- `set_player_difficulty` rescales each live hard point's current health by new / old Health_Multiplier, clamped to
  the new maximum, after the difficulty is changed.
