#include "search/planner.hpp"

#include "random_network.hpp"

#include "engine/core/observation.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <vector>

namespace {
const search::Game &game() {
  static const search::Game instance(TRAINERLAB_DATA_DIR);
  return instance;
}

search::Network smallNetwork() { return search::testing::randomNetwork(game()); }
} // namespace

TEST_CASE("the network masks illegal options and values within -1 and 1") {
  const search::Network net = smallNetwork();
  const engine::BattleState state = search::openings(game(), 1)[0];
  std::vector<float> mask(search::Encoding::kActions, 0.0f);
  mask[0] = mask[2] = 1.0f;

  const auto out = net.evaluate(engine::observe(state, 0), 0, mask.data());

  CHECK(out.logits[1] == -1e9f);
  CHECK(out.logits[0] > -1e9f);
  CHECK(std::abs(out.value) <= 1.0f);
}

TEST_CASE("a network made for another encoding is refused") {
  CHECK_THROWS_AS(search::Network(game().encoding(), {}), std::invalid_argument);
}

TEST_CASE("the planner plays a legal option, the same for the same seed") {
  const search::Network net = smallNetwork();
  const search::Planner planner(game(), net, {40, 4, 1.5});
  for (const engine::BattleState &state : search::openings(game(), 3)) {
    const engine::BattleState view = engine::observe(state, 0);
    const auto legal = game().rules().legalActions(state, 0);
    const int choice = planner.chooseAction(view, 0, legal, 9);
    CHECK(static_cast<size_t>(choice) < legal.size());
    CHECK(planner.chooseAction(view, 0, legal, 9) == choice);
  }
}

TEST_CASE("a position gets the same numbers alone and in any batch") {
  const search::Network net = smallNetwork();
  const std::vector<engine::BattleState> states = search::openings(game(), 9);
  std::vector<engine::BattleState> views;
  std::vector<std::vector<float>> masks;
  std::vector<search::Network::Query> queries;
  for (size_t i = 0; i < states.size(); ++i) {
    const int side = static_cast<int>(i % 2);
    views.push_back(engine::observe(states[i], side));
    std::vector<float> mask(search::Encoding::kActions);
    search::Encoding::mask(game().rules().legalActions(states[i], side), mask.data());
    masks.push_back(mask);
  }
  for (size_t i = 0; i < states.size(); ++i)
    queries.push_back({&views[i], static_cast<int>(i % 2), masks[i].data()});

  std::vector<search::Network::Output> batch(queries.size());
  net.evaluate(queries, batch.data());
  for (size_t i = 0; i < queries.size(); ++i) {
    const auto alone = net.evaluate(views[i], queries[i].side, masks[i].data());
    CHECK(alone.logits == batch[i].logits); // bit for bit
    CHECK(alone.value == batch[i].value);
  }
}
