#pragma once

#include "engine/effects/effect.hpp"
#include "engine/model/field.hpp"

namespace engine {
class SetHazardEffect : public Effect {
public:
  explicit SetHazardEffect(HazardKind hazard) : hazard_(hazard) {}
  void apply(EffectContext &ctx) const override;
  const char *name() const override { return "SetHazard"; }

private:
  HazardKind hazard_;
};
} // namespace engine
