#pragma once

// A network of the shapes of trainerlab.rl.net, with small fixed pseudo-random weights: enough to
// test the C++ network and search, and to time them (the time does not depend on the values).

#include "search/game.hpp"
#include "search/network.hpp"

#include <map>
#include <string>
#include <vector>

namespace search::testing {
inline Network randomNetwork(const Game &game) {
  const auto &e = game.encoding();
  const int p = static_cast<int>(e.pokemonNames().size());
  const int f = static_cast<int>(e.fieldNames().size());
  const auto &data = game.data();
  const int emb = 16, hidden = 128, trunk = 256;
  const int moveWidth = 27;
  std::map<std::string, Network::Tensor> w;
  unsigned state = 1;
  auto tensor = [&](const std::string &name, std::vector<int> shape) {
    Network::Tensor t{shape, {}};
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
  return Network(e, w);
}
} // namespace search::testing
