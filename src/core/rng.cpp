#include "engine/core/rng.hpp"

#include <cstdint>

namespace engine {
MersenneRNG::MersenneRNG(uint64_t seed) : gen_(seed) {}

// Hand-written on top of mt19937_64, whose output the standard fixes
// bit for bit. std::uniform_*_distribution is implementation-defined: the
// same seed would give a different battle under libc++ (macOS) and
// libstdc++ (the Linux container).
int MersenneRNG::rangeInt(int min, int max) {
  if (max <= min)
    return min;
  const uint64_t span = static_cast<uint64_t>(static_cast<int64_t>(max) - min) + 1;
  // Rejection sampling: drop the top sliver that would bias the modulo.
  const uint64_t limit = UINT64_MAX - UINT64_MAX % span;
  uint64_t draw;
  do {
    draw = gen_();
  } while (draw >= limit);
  return static_cast<int>(static_cast<int64_t>(min) + static_cast<int64_t>(draw % span));
}

float MersenneRNG::unit() {
  // Top 24 bits -> [0, 1) on the float grid, never 1.0.
  return static_cast<float>(gen_() >> 40) * (1.0f / 16777216.0f);
}
} // namespace engine
