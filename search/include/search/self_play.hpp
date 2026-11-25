#pragma once

#include "search/game.hpp"
#include "search/network.hpp"
#include "search/planner.hpp"

#include <array>
#include <atomic>
#include <cstdint>
#include <stdexcept>
#include <utility>
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
  // Palier 10.4: after a knock-out with a choice, each option searched replacementSimulations
  // simulations (no noise) and the best sent in, a row to learn from: the replacing side's view,
  // its switches, the chosen one as the policy, no foe's option (foe -1). 0: the network's
  // favourite, unrecorded.
  int replacementSimulations = 0;
  // Palier 10.5: the teams' sizes, each with its share of the battles (summing to 1), drawn from
  // the battle's number; both sides alike, the random battle's teams cut to that size.
  std::vector<std::pair<int, double>> teamSizes{{engine::kTeamSize, 1.0}};
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
  std::vector<int32_t> foe;     // rows: the option the foe played that turn (Encoding's numbering)
  std::vector<float> foeMask;   // rows x 10: the foe's legal options that turn
  std::vector<int32_t> game;    // rows: the battle each row comes from
  std::vector<int32_t> turns;   // per battle
  std::vector<int8_t> winner;   // per battle: 0, 1, or -1 for a draw
  std::vector<int8_t> teamSize; // per battle: the size of the teams

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
                      int games, uint64_t seed, int threads);

// Palier 12.3: the battles of a measure, one bot on both sides (rule 11 with this network, as
// trainerlab.rl.planner plays it), played side by side as self-play's.
struct MatchSettings {
  PlannerSettings planner{};       // rule 11's: 200 simulations, no noise
  int replacementSimulations = 50; // per option of a replacement with a choice; 0: the favourite
  int lanes = 16;
  int maxTurns = 1000; // then a draw
};

struct MatchResult {
  std::vector<int8_t> winner; // per battle: 0, 1, or -1 for a draw
  std::vector<int32_t> turns;
};

// A measure's battle: each side's species (engine ids, the lead first), and its seed.
struct Match {
  std::array<std::vector<int>, 2> teams;
  uint64_t seed = 0;
};

// The battle set up as Python's referee sets it up (BattleEnvironment): each team built and
// checked, then started with the seed.
engine::BattleState matchStart(const Match &match);

// Thrown when a measure is asked to stop (Ctrl-C).
struct Stopped : std::runtime_error {
  Stopped() : std::runtime_error("playMatches: stopped") {}
};

// Each battle plays as Python's referee plays it (ai/src/trainerlab/game/environment.py) with its
// seed: the dice of turn t from seed * 1'000'003 + t, the faster side replacing first. Each side
// plays the option its search visits most, the searches seeded seed * 1'000'003 + 0, 1, 2… in the
// order they are made; a replacement with a choice searches each option, from one such seed
// onwards (Planner::searchReplacement).
//
// Thread t plays battles t, t + threads, t + 2 threads… `lanes` at a time, side by side, their
// network queries batched; as soon as one ends, the next of its battles takes its place. `done`,
// if given, counts the battles over; `stop`, once true, ends the measure (Stopped). The same
// results for the same number of threads; with our kernel, for any number of threads and lanes.
MatchResult playMatches(const Game &game, const Network &network, const MatchSettings &settings,
                        const std::vector<Match> &matches, int threads,
                        std::atomic<int> *done = nullptr, const std::atomic<bool> *stop = nullptr);
} // namespace search
