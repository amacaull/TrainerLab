#pragma once

#include "engine/effects/effect.hpp"

namespace engine {

// Disguise takes the first damaging hit in the holder's place: no damage,
// a 1/8 max-HP chip, and the rest of a single-hit chain stops. Returns true
// when the hit was absorbed. Shared by DamageEffect and FixedDamageEffect.
bool absorbedByDisguise(EffectContext &ctx);

class DamageEffect : public Effect {
public:
  void apply(EffectContext &ctx) const override;
  const char *name() const override { return "Damage"; }
};

} // namespace engine
