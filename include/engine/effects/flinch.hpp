#pragma once

#include "engine/effects/effect.hpp"

namespace engine {

// Sets the flinched volatile; only matters if the target has not acted yet
// this turn (checked in passesBeforeMove). Cleared at end of turn.
class FlinchEffect : public Effect {
public:
  void apply(EffectContext &ctx) const override;
  const char *name() const override { return "Flinch"; }
};

} // namespace engine
