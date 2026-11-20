#pragma once

// A network of the shapes of trainerlab.rl.net, with small fixed pseudo-random weights: enough to
// test the C++ network and search, and to time them (the time does not depend on the values).

#include "search/game.hpp"
#include "search/network.hpp"

#include <map>
#include <string>
#include <vector>

namespace search::testing {
// `layers` of attention (palier 10) over the 12 Pokemon and the field, if any.
inline Network randomNetwork(const Game &game, int layers = 0) {
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
  linear("trunk.0", 4 * hidden + (layers ? hidden : f), trunk);
  linear("trunk.2", trunk, trunk);
  linear("moves.0", trunk + emb + moveWidth, hidden);
  linear("moves.2", hidden, 1);
  linear("switch.0", trunk + hidden, hidden);
  linear("switch.2", hidden, 1);
  linear("value", trunk, 1);
  auto norm = [&](const std::string &name) {
    w[name + ".weight"] = {{hidden}, std::vector<float>(static_cast<size_t>(hidden), 1.0f)};
    tensor(name + ".bias", {hidden});
  };
  for (int i = 0; i < layers; ++i) {
    const std::string at = "attention." + std::to_string(i) + ".";
    norm(at + "norm1");
    norm(at + "norm2");
    linear(at + "qkv", hidden, 3 * hidden);
    linear(at + "out", hidden, hidden);
    linear(at + "ffn.0", hidden, 256);
    linear(at + "ffn.2", 256, hidden);
  }
  if (layers) {
    linear("field_token", f, hidden);
    tensor("slots", {13, hidden});
    norm("norm");
  }
  return Network(e, w);
}
} // namespace search::testing
