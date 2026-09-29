#pragma once

#include "engine/effects/effect.hpp"
#include "engine/model/status.hpp"

namespace engine {

// Applies its status when it resolves (accuracy is checked upstream). A
// "chance" in the JSON wraps it as a secondary (10% burn on Flamethrower).
class ApplyStatusEffect : public Effect {
public:
  explicit ApplyStatusEffect(Status status) : status_(status) {}
  void apply(EffectContext &ctx) const override;
  const char *name() const override { return "ApplyStatus"; }

private:
  Status status_;
};

} // namespace engine
