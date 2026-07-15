#pragma once

#include "engine/effect.hpp"

namespace engine {

// User takes 1/denominator of the damage actually dealt (Brave Bird 3,
// Flare Blitz 3). Reads EffectContext.lastDamageDealt; recoil can KO.
class RecoilEffect : public Effect {
public:
  explicit RecoilEffect(int denominator) : denominator_(denominator) {}
  void apply(EffectContext &ctx) const override;
  const char *name() const override { return "Recoil"; }

private:
  int denominator_;
};

} // namespace engine
