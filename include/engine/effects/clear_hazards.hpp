#pragma once

#include "engine/effect.hpp"

namespace engine {

// Rapid Spin (user's side only) and Defog (both sides, gen 6+).
class ClearHazardsEffect : public Effect {
public:
  explicit ClearHazardsEffect(bool bothSides) : bothSides_(bothSides) {}
  void apply(EffectContext &ctx) const override;
  const char *name() const override { return "ClearHazards"; }

private:
  bool bothSides_;
};

} // namespace engine
