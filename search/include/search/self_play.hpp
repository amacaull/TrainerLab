#pragma once

#include "search/game.hpp"
#include "search/network.hpp"
#include "search/planner.hpp"

#include <cstdint>
#include <vector>

namespace search {
// Fixed before any measurement.
struct SelfPlaySettings {
  // The full searches: their decisions are recorded, to learn from.
  PlannerSettings planner{800, 8, 1.5, 0.25};
  int maxTurns = 1000; // then a draw, as in VecEnv
  // Palier 8.3: battles each thread plays side by side, their network queries batched (the M4's
  // matrix unit is fastest on large products). Battles g*lanes to g*lanes + lanes - 1 go
  // together, whatever the threads.
  int lanes = 16;
  // Palier 8.5, KataGo's playout cap randomization: a share `fullShare` of the searches are full;
  // the others search `fast` simulations without noise, and only move the battle on, unrecorded.
  // 0: every search full.
  int fast = 100;
  double fullShare = 0.25;
  // Palier 10.3: endgames. Each battle keeps its first position with endgameAlive Pokemon or fewer
  // standing on each side (a turn to play, no replacement pending); given such positions, a share
  // endgameShare of the battles start from one of them, with dice of their own.
  int endgameAlive = 3;
  double endgameShare = 0.25;
};

// What self-play leaves to learn from, one row per decision: the encoding of what the side saw,
// its mask, the share of the search's visits each option got (the policy to learn), and the
// result of the battle for that side (the value to learn: 1, -1 or 0 for a draw).
struct SelfPlayData {
  std::vector<float> pokemon;     // rows x 12 x P
  std::vector<int32_t> ids;       // rows x 12 x 7
  std::vector<float> field;       // rows x F
  std::vector<float> mask;        // rows x 10
  std::vector<float> policy;      // rows x 10
  std::vector<float> result;      // rows
  std::vector<float> searchValue; // rows: the search's mean result for the side, over its visits
  std::vector<int32_t> foe;   // rows: the option the foe played that turn (Encoding's numbering)
  std::vector<float> foeMask; // rows x 10: the foe's legal options that turn
  std::vector<int32_t> game;  // rows: the battle each row comes from
  std::vector<int32_t> turns; // per battle
  std::vector<int8_t> winner; // per battle: 0, 1, or -1 for a draw
  std::vector<int8_t> fromEndgame;           // per battle: 1 if it started from an endgame given
  std::vector<engine::BattleState> endgames; // the battles' first endgames, in battle order

  int rows() const { return static_cast<int>(result.size()); }
};

// The AlphaZero loop's games: rule 11 against itself, with the network given. Each side searches
// what it sees and plays an option drawn in proportion to its visits, so that games vary and mixed
// choices stay mixed; a full search, with noise at the root, is recorded, a fast one is not. A
// side with one legal option plays it, unrecorded; a fainted Pokemon is replaced by the network's
// favourite, unrecorded.
//
// Battle g starts from the seed of VecEnv's battle g (seed * 1'000'003 + g). The groups of
// `lanes` battles are shared among `threads` threads; the rows come in battle order, the same for
// any number of threads.
SelfPlayData selfPlay(const Game &game, const Network &network, const SelfPlaySettings &settings,
                      int games, uint64_t seed, int threads,
                      const std::vector<engine::BattleState> &endgames = {});
} // namespace search
