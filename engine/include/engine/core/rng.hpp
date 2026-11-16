#pragma once

#include <cstdint>
#include <initializer_list>
#include <random>

namespace engine {
// Always injected, never rand(): the tests replay exact outcomes with FixedRNG.
class RNG {
public:
  virtual ~RNG() = default;
  virtual int rangeInt(int min, int max) = 0;
  virtual float unit() = 0;

  virtual bool chance(float p) { return unit() < p; }
  // Draws in [min, max] and returns the index of the first upper bound the draw does not exceed,
  // or the number of bounds if it exceeds them all. Bounds are increasing.
  virtual int bucket(int min, int max, std::initializer_list<int> upperBounds);
  // The damage roll, a percentage: uniform in [85, 100].
  virtual int damageRoll() { return rangeInt(85, 100); }

  bool chancePct(int p_pct) { return bucket(1, 100, {p_pct}) == 0; }
};

class MersenneRNG : public RNG {
public:
  explicit MersenneRNG(uint64_t seed);
  int rangeInt(int min, int max) override;
  float unit() override;

private:
  std::mt19937_64 gen_;
};

class FixedRNG : public RNG {
public:
  explicit FixedRNG(float unit_value = 0.0f) : unit_(unit_value) {}
  int rangeInt(int min, int /*max*/) override { return min; }
  float unit() override { return unit_; }

private:
  float unit_;
};
} // namespace engine
