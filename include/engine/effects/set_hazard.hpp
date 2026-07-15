#pragma once

#include "engine/effect.hpp"
#include "engine/field.hpp"

namespace engine {

// Stealth Rock, Spikes, Toxic Spikes: adds one layer on the OPPOSING side,
// fails at the canon cap (1 / 3 / 2).
class SetHazardEffect : public Effect {
public:
  explicit SetHazardEffect(HazardKind hazard) : hazard_(hazard) {}
  void apply(EffectContext &ctx) const override;
  const char *name() const override { return "SetHazard"; }

private:
  HazardKind hazard_;
};

} // namespace engine
