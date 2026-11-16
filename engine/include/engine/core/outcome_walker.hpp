#pragma once

#include "engine/core/rng.hpp"

#include <vector>

namespace engine {
// An RNG that walks every sequence of draws a computation can make, depth first, with the
// probability of each. A run follows the choices recorded so far and takes the first option at any
// new draw; next() then moves on to the following sequence, like an odometer.
//
// Only draws through chance(), bucket(), rangeInt() and damageRoll() can be walked. The computation
// must be deterministic given its draws.
class OutcomeWalker : public RNG {
public:
  int rangeInt(int min, int max) override;
  float unit() override; // throws: a raw float has no finite set of outcomes
  bool chance(float p) override;
  int bucket(int min, int max, std::initializer_list<int> upperBounds) override;

  // The probability of the sequence the current run has followed so far.
  double weight() const { return weight_; }

  // Prepares the run of the next sequence; false once every sequence has been walked.
  bool next();

private:
  struct Draw {
    int choice;
    int options;
  };

  // The index this run takes among outcomes of the given probabilities. Impossible outcomes are
  // never taken, and a draw with one possible outcome is not a branch.
  int take(const std::vector<double> &probabilities);

  std::vector<Draw> path_;
  size_t depth_ = 0;
  double weight_ = 1.0;
};
} // namespace engine
