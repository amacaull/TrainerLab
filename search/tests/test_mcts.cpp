#include "search/mcts.hpp"

#include "engine/core/observation.hpp"
#include "engine/ffi/ffi.hpp"

#include <catch2/catch_test_macros.hpp>

#include <vector>

namespace {
const search::Game &game() {
  static const search::Game instance(TRAINERLAB_DATA_DIR);
  return instance;
}

// A model with every weight at 0 but one: the odds rise as the foe's HP fall.
search::Forecaster hpHunter() {
  const auto &names = game().features().names();
  search::WinModel model;
  model.weights.push_back(std::vector<double>(names.size(), 0.0));
  model.biases.push_back({0.0});
  model.mean.assign(names.size(), 0.0);
  model.std.assign(names.size(), 1.0);
  for (size_t i = 0; i < names.size(); ++i)
    if (names[i] == "foe_hp_total")
      model.weights[0][i] = -10.0;
  return search::Forecaster(game(), model);
}
} // namespace

TEST_CASE("the same seed makes the same decision") {
  const search::Forecaster evaluator = hpHunter();
  const search::Mcts mcts(game(), evaluator, {60, 3, 4, 1.0});
  for (const engine::BattleState &state : search::openings(game(), 4)) {
    const engine::BattleState view = engine::observe(state, 0);
    const std::vector<engine::Action> legal = game().rules().legalActions(state, 0);
    const int choice = mcts.chooseAction(view, 0, legal, 7);
    CHECK(choice >= 0);
    CHECK(static_cast<size_t>(choice) < legal.size());
    CHECK(mcts.chooseAction(view, 0, legal, 7) == choice);
  }
}

TEST_CASE("with one option, nothing to search") {
  const search::Forecaster evaluator = hpHunter();
  const search::Mcts mcts(game(), evaluator);
  const engine::BattleState state = search::openings(game(), 1)[0];
  const std::vector<engine::Action> one{game().rules().legalActions(state, 0).front()};

  CHECK(mcts.chooseAction(engine::observe(state, 0), 0, one, 1) == 0);
}

TEST_CASE("a search with every Pokemon seen still plays, from either side") {
  // Six against one, nothing hidden: the worlds are the view itself.
  const search::Forecaster evaluator = hpHunter();
  const search::Mcts mcts(game(), evaluator, {40, 2, 2, 1.0});
  engine::BattleState state = game().randomBattle(5);
  state.team_size[1] = 1;
  for (int side : {0, 1}) {
    const std::vector<engine::Action> legal = game().rules().legalActions(state, side);
    const int choice = mcts.chooseAction(engine::observe(state, side), side, legal, 3);
    CHECK(static_cast<size_t>(choice) < legal.size());
  }
}
