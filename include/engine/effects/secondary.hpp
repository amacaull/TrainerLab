#pragma once

#include "engine/effects/effect.hpp"

namespace engine {
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
