#pragma once

#include "search/forecaster.hpp"
#include "search/game.hpp"

#include "engine/core/battle_state.hpp"

#include <cstdint>
#include <vector>

namespace search {
// Fixed before any measurement.
struct MctsSettings {
  int iterations = 200; // simulations per decision
  int depth = 3;        // turns searched before the model rates the position
  int worlds = 8;       // guesses at the foe's unseen Pokemon, used in turn
  double explore = 1.0; // the weight of the exploration bonus (UCB1)
};

// Rule 9: a Monte Carlo tree search over simultaneous moves.
//
// Both sides choose at once, so each node of the tree keeps, for each side, the statistics of its
// own options (decoupled UCT): each side picks its option by UCB1 on its own statistics, and the
// pair of choices leads to the child. The foe's Pokemon not seen yet are guessed: before
// searching, `worlds` legal teams are drawn that keep every Pokemon already seen and fill the rest
// with species not seen; each simulation plays in one of them. A simulation goes down the tree,
// adds one node, and the model rates where it stopped, `depth` turns at most below the root.
// During the search, a fainted Pokemon is replaced by the best matchup (rule 3) on both sides.
//
// The option played is the one the search visited most. The seed fixes the worlds and the dice:
// the same seed makes the same decision.
class Mcts {
public:
  Mcts(const Game &game, const Forecaster &evaluator, MctsSettings settings = {});

  // The index, in `legal`, of the option played.
  int chooseAction(const engine::BattleState &view, int side,
                   const std::vector<engine::Action> &legal, uint64_t seed) const;

private:
  struct Node;

  // Plays one simulation from `state`, at `node`; returns the value for `root` (its odds).
  double simulate(engine::BattleState &state, Node &node, int depth, int root,
                  engine::RNG &dice) const;
  double rate(engine::BattleState &state, int root) const;
  void replaceFainted(engine::BattleState &state, engine::RNG &dice) const;
  std::vector<engine::BattleState> worlds(const engine::BattleState &view, int foe,
                                          uint64_t seed) const;

  const Game &game_;
  const Forecaster &evaluator_;
  MctsSettings settings_;
};
} // namespace search
