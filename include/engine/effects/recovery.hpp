#pragma once

#include "engine/effects/effect.hpp"

namespace engine {
// Fails at full HP, which also cancels Roost's type suppression (canon).
class RecoveryEffect : public Effect {
public:
  explicit RecoveryEffect(int denominator) : denominator_(denominator) {}
  void apply(EffectContext &ctx) const override;
  const char *name() const override { return "Recovery"; }

private:
  int denominator_;
};

class RoostEffect : public Effect {
public:
  void apply(EffectContext &ctx) const override;
  const char *name() const override { return "Roost"; }
};

// Exempt from the Sleep Clause; cures any prior status.
class RestEffect : public Effect {
public:
  void apply(EffectContext &ctx) const override;
  const char *name() const override { return "Rest"; }
};
} // namespace engine
