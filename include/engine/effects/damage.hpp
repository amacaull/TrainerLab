#pragma once

#include "engine/effects/effect.hpp"

namespace engine {
bool absorbedByDisguise(EffectContext &ctx);

class DamageEffect : public Effect {
public:
  void apply(EffectContext &ctx) const override;
  const char *name() const override { return "Damage"; }
};
} // namespace engine
