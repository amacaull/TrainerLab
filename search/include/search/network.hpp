#pragma once

#include "search/encoding.hpp"

#include "engine/core/battle_state.hpp"

#include <array>
#include <map>
#include <span>
#include <string>
#include <vector>

namespace search {
// The policy and value network of trainerlab.rl.net, evaluated in C++: the same layers, the same
// weights, in float. A tree search asks it thousands of times per decision, too often to go
// through Python and PyTorch.
//
// It evaluates positions in batches: each layer then reads its weights once for all the rows of
// the batch (12 Pokemon per position, 4 moves, 6 switches) instead of once per row. On macOS the
// products go through Accelerate (palier 8.2), whose sums may run in another order from one batch
// size to another: a position gets numbers a float's rounding apart. Our kernel, used elsewhere,
// sums each output's inputs in order, one by one: the same numbers alone or in any batch, on the
// same machine (the M4 fuses its multiply-adds). Either way, the same batches give the same
// numbers: self-play stays the same whatever its threads.
// y (rows x out, rows `out` apart) = x (rows x in, rows `stride` apart) times w (in x out,
// row-major): the network's own kernel, also timed by the benchmark against other libraries.
void multiply(const float *x, int rows, int stride, const float *w, int in, int out, float *y);

class Network {
public:
  struct Tensor {
    std::vector<int> shape;
    std::vector<float> values; // row-major, as PyTorch stores them
  };

  // The weights by their PyTorch names (species.weight, pokemon.0.weight...).
  Network(const Encoding &encoding, const std::map<std::string, Tensor> &weights);

  // What `side` sees in `view`, given the mask of its options.
  struct Query {
    const engine::BattleState *view;
    int side;
    const float *mask;
  };
  struct Output {
    std::array<float, Encoding::kActions> logits; // illegal options at -1e9
    float value;                                  // the expected result for `side`, in [-1, 1]
  };

  Output evaluate(const engine::BattleState &view, int side, const float *mask) const;
  // What multiplies the layers: "Accelerate" (macOS), "BLAS" or "kernel" (search::multiply).
  static const char *backend();

  // out[i] is what the network makes of queries[i].
  void evaluate(std::span<const Query> queries, Output *out) const;

private:
  struct Linear {
    int in = 0, out = 0;
    std::vector<float> weight; // in x out: PyTorch's out x in, transposed
    std::vector<float> bias;
    // For each of `rows` rows of x (`stride` apart), y = its `count` floats times the weights of
    // inputs [from, from + count): a share of the product, no bias. y rows are `out` apart.
    void partial(const float *x, int rows, int stride, int from, int count, float *y) const;
    // y = x times the weights, plus the bias, then max(., 0) if relu; x rows are `in` apart.
    void apply(const float *x, int rows, float *y, bool relu) const;
  };
  struct Embedding {
    int dim = 0;
    std::vector<float> table;
    const float *row(int32_t id) const {
      return table.data() + static_cast<size_t>(id) * static_cast<size_t>(dim);
    }
  };

  const Encoding &encoding_;
  Embedding species_, ability_, item_, move_;
  Linear pokemon1_, pokemon2_, trunk1_, trunk2_, moves1_, moves2_, switch1_, switch2_, value_;
  int present_, known_, active_, moveColumns_, moveWidth_;
  // What the Pokemon network makes of a foe not seen yet, and of an empty place: always the same
  // input, computed once.
  std::vector<float> unseen_, empty_;
};
} // namespace search
