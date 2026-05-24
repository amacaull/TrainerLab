#pragma once

#include "engine/effect.hpp"

namespace engine {

class DamageEffect : public Effect {
public:
  void apply(EffectContext &ctx) const override;
  const char *name() const override { return "Damage"; }
};

} // namespace engine
