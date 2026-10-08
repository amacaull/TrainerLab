#pragma once

#include "engine/effects/effect.hpp"

namespace engine {
class ClearHazardsEffect : public Effect {
public:
  explicit ClearHazardsEffect(bool bothSides, bool clearScreens = false)
      : bothSides_(bothSides), clearScreens_(clearScreens) {}
  void apply(EffectContext &ctx) const override;
  const char *name() const override { return "ClearHazards"; }

private:
  bool bothSides_;
  bool clearScreens_;
};
} // namespace engine
