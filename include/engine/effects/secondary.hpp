#pragma once

#include "engine/effect.hpp"

namespace engine {

// Wraps any effect with a proc chance ("chance": 30 in JSON). Rolls through
// RNG::chance(float) so FixedRNG can force (0.0) or deny (0.99) procs
// without touching accuracy rolls (ADR #17 / #26).
class SecondaryEffect : public Effect {
public:
  SecondaryEffect(float probability, EffectPtr inner)
      : probability_(probability), inner_(std::move(inner)) {}
  void apply(EffectContext &ctx) const override;
  const char *name() const override { return "Secondary"; }

private:
  float probability_;
  EffectPtr inner_;
};

} // namespace engine
