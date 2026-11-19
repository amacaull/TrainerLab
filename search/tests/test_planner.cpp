#include "search/planner.hpp"

#include "engine/core/observation.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <map>
#include <string>
#include <vector>

namespace {
const search::Game &game() {
  static const search::Game instance(TRAINERLAB_DATA_DIR);
  return instance;
}

// A network of the right shapes, with small fixed weights: enough to search with.
search::Network smallNetwork() {
  const auto &e = game().encoding();
  const int p = static_cast<int>(e.pokemonNames().size());
  const int f = static_cast<int>(e.fieldNames().size());
  const auto &data = game().data();
  const int emb = 16, hidden = 128, trunk = 256;
  const int moveWidth = 27;
  std::map<std::string, search::Network::Tensor> w;
  unsigned state = 1;
  auto tensor = [&](const std::string &name, std::vector<int> shape) {
    search::Network::Tensor t{shape, {}};
    size_t size = 1;
    for (int d : shape)
      size *= static_cast<size_t>(d);
    for (size_t i = 0; i < size; ++i) {
      state = state * 1664525u + 1013904223u;
      t.values.push_back(static_cast<float>((state >> 8) % 2001) / 1000.0f / 50.0f - 0.02f);
    }
    w[name] = t;
  };
  auto linear = [&](const std::string &name, int in, int out) {
    tensor(name + ".weight", {out, in});
    tensor(name + ".bias", {out});
  };
  tensor("species.weight", {data.speciesCount() + 1, emb});
  tensor("ability.weight", {46, emb});
  tensor("item.weight", {14, emb});
  tensor("move.weight", {data.moveCount() + 1, emb});
  linear("pokemon.0", p + 7 * emb, hidden);
  linear("pokemon.2", hidden, hidden);
  linear("trunk.0", 4 * hidden + f, trunk);
  linear("trunk.2", trunk, trunk);
  linear("moves.0", trunk + emb + moveWidth, hidden);
  linear("moves.2", hidden, 1);
  linear("switch.0", trunk + hidden, hidden);
  linear("switch.2", hidden, 1);
  linear("value", trunk, 1);
  return search::Network(game().encoding(), w);
}
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
