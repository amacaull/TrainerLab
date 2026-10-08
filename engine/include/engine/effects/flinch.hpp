#pragma once

#include "engine/effects/effect.hpp"

namespace engine {
class FlinchEffect : public Effect {
public:
  void apply(EffectContext &ctx) const override;
  const char *name() const override { return "Flinch"; }
};
} // namespace engine
