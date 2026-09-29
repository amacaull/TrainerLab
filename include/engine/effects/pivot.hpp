#pragma once

#include "engine/effects/effect.hpp"

namespace engine {
class PivotEffect : public Effect {
public:
  void apply(EffectContext &ctx) const override;
  const char *name() const override { return "Pivot"; }
};
} // namespace engine
