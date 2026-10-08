#pragma once

#include <cstdint>
#include <random>

namespace engine {
// Always injected, never rand(): the tests replay exact outcomes with FixedRNG.
class RNG {
public:
  virtual ~RNG() = default;
  virtual int rangeInt(int min, int max) = 0;
  virtual float unit() = 0;

  bool chance(float p) { return unit() < p; }
  bool chancePct(int p_pct) { return rangeInt(1, 100) <= p_pct; }
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
