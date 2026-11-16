#include "engine/core/outcome_walker.hpp"

#include <algorithm>
#include <stdexcept>

namespace engine {
int OutcomeWalker::take(const std::vector<double> &probabilities) {
  std::vector<int> possible;
  for (size_t i = 0; i < probabilities.size(); ++i)
    if (probabilities[i] > 0.0)
      possible.push_back(static_cast<int>(i));
  if (possible.size() == 1)
    return possible.front();
  if (depth_ == path_.size())
    path_.push_back(Draw{0, static_cast<int>(possible.size())});
  const int index = possible[static_cast<size_t>(path_[depth_++].choice)];
  weight_ *= probabilities[static_cast<size_t>(index)];
  return index;
}

int OutcomeWalker::rangeInt(int min, int max) {
  if (max <= min)
    return min;
  const int count = max - min + 1;
  return min + take(std::vector<double>(static_cast<size_t>(count), 1.0 / count));
}

float OutcomeWalker::unit() {
  throw std::logic_error("OutcomeWalker: unit() cannot be walked, draw through chance()");
}

bool OutcomeWalker::chance(float p) {
  const double yes = std::clamp(static_cast<double>(p), 0.0, 1.0);
  return take({yes, 1.0 - yes}) == 0;
}

int OutcomeWalker::bucket(int min, int max, std::initializer_list<int> upperBounds) {
  const double span = max - min + 1;
  std::vector<double> probabilities;
  int below = min - 1; // the last value already given to a bucket
  for (int bound : upperBounds) {
    const int top = std::clamp(bound, below, max);
    probabilities.push_back((top - below) / span);
    below = top;
  }
  probabilities.push_back((max - below) / span);
  return take(probabilities);
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
