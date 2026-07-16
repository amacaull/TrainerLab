#include "engine/core/rng.hpp"

namespace engine {

MersenneRNG::MersenneRNG(uint64_t seed) : gen_(seed) {}

int MersenneRNG::rangeInt(int min, int max) {
  std::uniform_int_distribution<int> dist(min, max);
  return dist(gen_);
}

float MersenneRNG::unit() {
  std::uniform_real_distribution<float> dist(0.0f, 1.0f);
  return dist(gen_);
}

} // namespace engine
