#include "engine/core/outcome_walker.hpp"

#include <algorithm>
#include <array>
#include <stdexcept>

namespace engine {
int OutcomeWalker::take(const double *probabilities, int count) {
  std::array<int, kMaxOutcomes> possible{};
  int options = 0;
  for (int i = 0; i < count; ++i)
    if (probabilities[i] > 0.0)
      possible[static_cast<size_t>(options++)] = i;
  if (options == 1)
    return possible[0];
  if (depth_ == path_.size())
    path_.push_back(Draw{0, options});
  const int index = possible[static_cast<size_t>(path_[depth_++].choice)];
  weight_ *= probabilities[index];
  return index;
}

int OutcomeWalker::rangeInt(int min, int max) {
  if (max <= min)
    return min;
  const int count = max - min + 1;
  if (count > kMaxOutcomes)
    throw std::logic_error("OutcomeWalker: a draw among more than kMaxOutcomes values");
  std::array<double, kMaxOutcomes> probabilities{};
  std::fill_n(probabilities.begin(), count, 1.0 / count);
  return min + take(probabilities.data(), count);
}

float OutcomeWalker::unit() {
  throw std::logic_error("OutcomeWalker: unit() cannot be walked, draw through chance()");
}

bool OutcomeWalker::chance(float p) {
  const double yes = std::clamp(static_cast<double>(p), 0.0, 1.0);
  const double probabilities[] = {yes, 1.0 - yes};
  return take(probabilities, 2) == 0;
}

int OutcomeWalker::bucket(int min, int max, std::initializer_list<int> upperBounds) {
  if (upperBounds.size() + 1 > kMaxOutcomes)
    throw std::logic_error("OutcomeWalker: a draw among more than kMaxOutcomes buckets");
  const double span = max - min + 1;
  std::array<double, kMaxOutcomes> probabilities{};
  int count = 0;
  int below = min - 1; // the last value already given to a bucket
  for (int bound : upperBounds) {
    const int top = std::clamp(bound, below, max);
    probabilities[static_cast<size_t>(count++)] = (top - below) / span;
    below = top;
  }
  probabilities[static_cast<size_t>(count++)] = (max - below) / span;
  return take(probabilities.data(), count);
}

bool OutcomeWalker::next() {
  while (!path_.empty() && path_.back().choice + 1 == path_.back().options)
    path_.pop_back();
  depth_ = 0;
  weight_ = 1.0;
  if (path_.empty())
    return false;
  ++path_.back().choice;
  return true;
}
} // namespace engine
