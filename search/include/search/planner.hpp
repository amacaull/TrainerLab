#pragma once

#include "search/game.hpp"
#include "search/network.hpp"

#include "engine/core/battle_state.hpp"
#include "engine/core/rng.hpp"

#include <cstdint>
#include <vector>

namespace search {
// Fixed before any measurement.
struct PlannerSettings {
  int simulations = 200; // per decision
  int worlds = 8;        // guesses at the foe's unseen Pokemon, used in turn
  double explore = 1.5;  // the weight of the prior in PUCT
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

private:
  struct Node;

  double simulate(engine::BattleState &state, Node &node, int root, engine::RNG &dice) const;
  double expand(const engine::BattleState &state, Node &node, int root) const;
  void replaceFainted(engine::BattleState &state, engine::RNG &dice) const;

  const Game &game_;
  const Network &network_;
  PlannerSettings settings_;
};
} // namespace search
