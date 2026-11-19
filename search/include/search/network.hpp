#pragma once

#include "search/encoding.hpp"

#include "engine/core/battle_state.hpp"

#include <array>
#include <map>
#include <string>
#include <vector>

namespace search {
// The policy and value network of trainerlab.rl.net, evaluated in C++: the same layers, the same
// weights, in float. A tree search asks it thousands of times per decision, too often to go
// through Python and PyTorch.
class Network {
public:
  struct Tensor {
    std::vector<int> shape;
    std::vector<float> values; // row-major, as PyTorch stores them
  };

  // The weights by their PyTorch names (species.weight, pokemon.0.weight...).
  Network(const Encoding &encoding, const std::map<std::string, Tensor> &weights);

  struct Output {
    std::array<float, Encoding::kActions> logits; // illegal options at -1e9
    float value;                                  // the expected result for `side`, in [-1, 1]
  };

  // What the network makes of what `side` sees in `view`, given the mask of its options.
  Output evaluate(const engine::BattleState &view, int side, const float *mask) const;

private:
  struct Linear {
    int in = 0, out = 0;
    std::vector<float> weight; // out x in
    std::vector<float> bias;
    void apply(const float *x, float *y, bool relu) const;
    // y = the weights of inputs [from, from + count) times x: a share of the product, no bias.
    void partial(const float *x, int from, int count, float *y) const;
  };
  struct Embedding {
    int dim = 0;
    std::vector<float> table;
    const float *row(int32_t id) const { return table.data() + static_cast<size_t>(id) * dim; }
  };

  const Encoding &encoding_;
  Embedding species_, ability_, item_, move_;
  Linear pokemon1_, pokemon2_, trunk1_, trunk2_, moves1_, moves2_, switch1_, switch2_, value_;
  int present_, active_, moveColumns_, moveWidth_;
};
} // namespace search
