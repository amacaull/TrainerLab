#pragma once

#include "engine/effects/effect.hpp"
#include "engine/model/status.hpp"

namespace engine {
// Consecutive uses succeed with probability 1/3^n; the chain resets after any turn without a
// successful Protect.
class ProtectEffect : public Effect {
public:
  explicit ProtectEffect(Status contactStatus = Status::None) : contactStatus_(contactStatus) {}
  void apply(EffectContext &ctx) const override;
  const char *name() const override { return "Protect"; }

private:
  Status contactStatus_;
};
} // namespace engine
