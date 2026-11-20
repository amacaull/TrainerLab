#pragma once

#include "search/game.hpp"
#include "search/network.hpp"
#include "search/planner.hpp"

#include <cstdint>
#include <vector>

namespace search {
// Fixed before any measurement.
struct SelfPlaySettings {
  PlannerSettings planner{200, 8, 1.5, 0.25};
  int maxTurns = 1000; // then a draw, as in VecEnv
  // Palier 8.3: battles each thread plays side by side, their network queries batched (the M4's
  // matrix unit is fastest on large products). Battles g*lanes to g*lanes + lanes - 1 go
  // together, whatever the threads.
  int lanes = 16;
};

// What self-play leaves to learn from, one row per decision: the encoding of what the side saw,
// its mask, the share of the search's visits each option got (the policy to learn), and the
// result of the battle for that side (the value to learn: 1, -1 or 0 for a draw).
struct SelfPlayData {
  std::vector<float> pokemon; // rows x 12 x P
  std::vector<int32_t> ids;   // rows x 12 x 7
  std::vector<float> field;   // rows x F
  std::vector<float> mask;    // rows x 10
  std::vector<float> policy;  // rows x 10
  std::vector<float> result;  // rows
  std::vector<int32_t> game;  // rows: the battle each row comes from
  std::vector<int32_t> turns; // per battle
  std::vector<int8_t> winner; // per battle: 0, 1, or -1 for a draw

  int rows() const { return static_cast<int>(result.size()); }
};

// The AlphaZero loop's games: rule 11 against itself, with the network given. Each side searches
// what it sees, with noise at the root, and plays an option drawn in proportion to its visits, so
// that games vary and mixed choices stay mixed. A side with one legal option plays it, unrecorded;
// a fainted Pokemon is replaced by the network's favourite, unrecorded.
//
// Battle g starts from the seed of VecEnv's battle g (seed * 1'000'003 + g). The groups of
// `lanes` battles are shared among `threads` threads; the rows come in battle order, the same for
// any number of threads.
SelfPlayData selfPlay(const Game &game, const Network &network, const SelfPlaySettings &settings,
                      int games, uint64_t seed, int threads);
} // namespace search
