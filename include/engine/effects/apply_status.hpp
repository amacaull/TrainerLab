#pragma once

#include "engine/effects/effect.hpp"
#include "engine/model/status.hpp"

namespace engine {
class ApplyStatusEffect : public Effect {
public:
  explicit ApplyStatusEffect(Status status) : status_(status) {}
  void apply(EffectContext &ctx) const override;
  const char *name() const override { return "ApplyStatus"; }

private:
  Status status_;
};
} // namespace engine
