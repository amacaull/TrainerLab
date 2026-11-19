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

TEST_CASE("the planner's visits sit on the legal options; noise moves them") {
  const search::Network net = smallNetwork();
  const engine::BattleState state = search::openings(game(), 1)[0];
  const engine::BattleState view = engine::observe(state, 0);
  const auto legal = game().rules().legalActions(state, 0);
  std::vector<float> mask(search::Encoding::kActions);
  search::Encoding::mask(legal, mask.data());

  const search::Planner plain(game(), net, {60, 4, 1.5, 0.0});
  const search::Planner noisy(game(), net, {60, 4, 1.5, 0.9});
  const auto counts = plain.visits(view, 0, legal, 5);
  int total = 0;
  for (size_t a = 0; a < counts.size(); ++a) {
    CHECK((mask[a] == 1.0f || counts[a] == 0));
    total += counts[a];
  }
  CHECK(total == 59); // the first simulation only expands the root
  CHECK(noisy.visits(view, 0, legal, 5) != counts);
  CHECK(plain.visits(view, 0, legal, 5) == counts);

  const auto one = plain.visits(view, 0, {legal[0]}, 5); // nothing to search
  int sum = 0;
  for (int n : one)
    sum += n;
  CHECK(sum == 1);
}

TEST_CASE("the planner's analysis gives each option its visits and its mean value for the side") {
  const search::Network net = smallNetwork();
  const engine::BattleState state = search::openings(game(), 2)[1];
  const engine::BattleState view = engine::observe(state, 0);
  const auto legal = game().rules().legalActions(state, 0);
  std::vector<float> mask(search::Encoding::kActions);
  search::Encoding::mask(legal, mask.data());

  const search::Planner plain(game(), net, {80, 4, 1.5, 0.0});
  const auto analysis = plain.analyse(view, 0, legal, 3);
  CHECK(analysis.visits == plain.visits(view, 0, legal, 3));
  for (size_t a = 0; a < mask.size(); ++a) {
    CHECK(analysis.value[a] >= -1.0);
    CHECK(analysis.value[a] <= 1.0);
    if (analysis.visits[a] == 0)
      CHECK(analysis.value[a] == 0.0);
  }

  // A judge: every legal option visited at least 20 times, whatever its prior.
  const search::Planner judge(game(), net, {300, 4, 1.5, 0.0, 20});
  const auto judged = judge.analyse(view, 0, legal, 3);
  for (size_t a = 0; a < mask.size(); ++a)
    CHECK((mask[a] == 1.0f ? judged.visits[a] >= 20 : judged.visits[a] == 0));
}
