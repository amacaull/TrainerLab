#pragma once

#include "engine/core/battle_state.hpp"
#include "engine/core/data_loader.hpp"
#include "engine/core/engine.hpp"

#include <string>
#include <vector>

namespace search {
// What a side sees, as numbers: the C++ twin of trainerlab.learning.features, value for value.
//
// The Python module stays the reference: a test compares the two on thousands of positions, bit
// for bit, so that a model trained on Python features plays the same with these. Every value is
// computed in double, in the same order as in Python, then rounded once to float.
class Features {
public:
  Features(const engine::DataLoader &data, const engine::BattleEngine &rules);

  // The name of each column, in order: the same as feature_names() in Python.
  const std::vector<std::string> &names() const { return names_; }

  // What `side` saw in `view`, its own observe(), into `out` (names().size() floats).
  void compute(const engine::BattleState &view, int side, float *out) const;

private:
  double effectiveSpeed(const engine::BattlePokemon &pokemon) const;
  double bestDamage(const engine::BattleState &view, int side) const;

  const engine::BattleEngine &rules_;
  int species_;
  int choiceScarf_;
  std::vector<std::string> names_;
};
} // namespace search
