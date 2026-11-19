#pragma once

#include "search/game.hpp"
#include "search/network.hpp"

#include "engine/core/battle_state.hpp"
#include "engine/core/rng.hpp"

#include <array>
#include <cstdint>
#include <vector>

namespace search {
// Fixed before any measurement.
struct PlannerSettings {
  int simulations = 200; // per decision
  int worlds = 8;        // guesses at the foe's unseen Pokemon, used in turn
  double explore = 1.5;  // the weight of the prior in PUCT
  // Self-play only (rule 11 plays with 0): the share of the root's prior replaced by noise, a flat
  // Dirichlet draw over the options, so that the search also tries what the network neglects.
  double noise = 0.0;
  // A judge's setting (0 to play): every legal option of the side searched is tried at least
  // this many times at the root, so that even an option the network dislikes gets a value.
  int minRootVisits = 0;
};

// Rule 11: a tree search over simultaneous moves guided by the policy and value network, as in
// AlphaZero.
//
// The tree is rule 9's: each node keeps, for each side, the statistics of its own options, and
// the pair of choices leads to the child; the foe's unseen Pokemon are guessed as legal teams.
// What changes is the network. A node is expanded at its first visit: the network gives each
// side's policy over its options (the priors) and the value of the position, which is backed up
// at once, without playing further. Each side then picks by PUCT:
//
//   Q(a) + explore x P(a) x sqrt(N) / (1 + n(a))
//
// an option never tried starting from the node's value. A fainted Pokemon is replaced by the
// network's favourite switch. The option played is the one the search visited most.
class Planner {
public:
  Planner(const Game &game, const Network &network, PlannerSettings settings = {});

  // The index, in `legal`, of the option played. The seed fixes the worlds and the dice.
  int chooseAction(const engine::BattleState &view, int side,
                   const std::vector<engine::Action> &legal, uint64_t seed) const;

  // How many times the search visited each of `side`'s ten options (Encoding's numbering) at the
  // root: what self-play learns from. With one legal option, no search: all on that option.
  std::array<int, Encoding::kActions> visits(const engine::BattleState &view, int side,
                                             const std::vector<engine::Action> &legal,
                                             uint64_t seed) const;

  // At the root, per option of `side` (Encoding's numbering): its visits, and the mean result
  // for `side` of the simulations that tried it (0 if none did).
  struct Analysis {
    std::array<int, Encoding::kActions> visits{};
    std::array<double, Encoding::kActions> value{};
  };
  Analysis analyse(const engine::BattleState &view, int side,
                   const std::vector<engine::Action> &legal, uint64_t seed) const;

  // The team index of the network's favourite Pokemon to send in, without search.
  int chooseReplacement(const engine::BattleState &view, int side) const;

private:
  struct Node;

  double simulate(engine::BattleState &state, Node &node, int root, engine::RNG &dice,
                  bool atRoot) const;
  double expand(const engine::BattleState &state, Node &node, int root) const;
  void replaceFainted(engine::BattleState &state, engine::RNG &dice) const;
  void addNoise(std::array<double, Encoding::kActions> &prior,
                const std::vector<engine::Action> &legal, uint64_t seed) const;

  const Game &game_;
  const Network &network_;
  PlannerSettings settings_;
};
} // namespace search
