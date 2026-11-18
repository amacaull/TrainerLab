#pragma once

#include "search/game.hpp"

#include "engine/core/battle_state.hpp"

#include <vector>

namespace search {
// A win probability model: standardised features, ReLU layers, then a sigmoid. The C++ twin of
// trainerlab.learning.model.WinModel; with no hidden layer, a logistic regression.
struct WinModel {
  // Layer by layer: weights[l] is inputs x outputs, row by row.
  std::vector<std::vector<double>> weights;
  std::vector<std::vector<double>> biases;
  std::vector<double> mean;
  std::vector<double> std;

  // The probability of winning, from one row of features.
  double predict(const float *features) const;
};

// Rules 6, 7 and 8 in C++; 6 and 7 make the same choices as their former Python version.
//
// For each option, the bot plays the turn on a copy of what it sees (each foe Pokemon not yet seen
// filled with a copy of the foe's active), twice, on seeds turn x 2 and turn x 2 + 1, and rates the
// positions reached with the model. The forecaster assumes the foe plays as rule 5; the strategist
// rates each option by the foe's reply that leaves it the lowest odds; nash plays the mixed
// strategy of the simultaneous game those odds define.
class Forecaster {
public:
  Forecaster(const Game &game, WinModel model);

  // The index, in `legal`, of the option played.
  int chooseAction(const engine::BattleState &view, int side,
                   const std::vector<engine::Action> &legal) const;
  int chooseStrategistAction(const engine::BattleState &view, int side,
                             const std::vector<engine::Action> &legal) const;
  // Rule 8: one probability per option in `legal`, the optimal mixed strategy of the zero-sum game
  // whose payoffs are the odds after each pair (option, foe reply).
  std::vector<double> nashStrategy(const engine::BattleState &view, int side,
                                   const std::vector<engine::Action> &legal) const;
  // The index, in `options`, of the Pokemon sent in.
  int chooseReplacement(const engine::BattleState &view, int side,
                        const std::vector<int> &options) const;

  // The probability of winning from this position, as the model sees it.
  double odds(const engine::BattleState &state, int side) const;

  // What rule 5 plays in `side`'s place, from its own view: the foe the forecaster assumes.
  engine::Action calculatorAction(const engine::BattleState &view, int side,
                                  const std::vector<engine::Action> &legal) const;

private:
  double afterTurn(const engine::BattleState &state, int side, const engine::Action &action,
                   const engine::Action &foeAction) const;
  engine::BattleState playable(const engine::BattleState &view, int foe) const;
  // The foe's moves, and its switches to Pokemon already seen: a stand-in is nothing to reason on.
  std::vector<engine::Action> replies(const engine::BattleState &view,
                                      const engine::BattleState &state, int foe) const;

  const Game &game_;
  WinModel model_;
};
} // namespace search
