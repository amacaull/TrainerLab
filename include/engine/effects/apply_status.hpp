#pragma once

#include "engine/effect.hpp"
#include "engine/status.hpp"

namespace engine {

// Phase 2: always applies when it resolves (accuracy is checked upstream).
// Probabilistic secondary statuses (10% burn on Flamethrower...) come in phase 8.
class ApplyStatusEffect : public Effect {
public:
  explicit ApplyStatusEffect(Status status) : status_(status) {}
  void apply(EffectContext &ctx) const override;
  const char *name() const override { return "ApplyStatus"; }

private:
  Status status_;
};

} // namespace engine
