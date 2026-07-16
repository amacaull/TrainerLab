#pragma once

#include "engine/effects/effect.hpp"

namespace engine {

// Heals maxHp/denominator (Recover, Roost: 2). Fails at full HP, which also
// stops the rest of the chain (Roost's type suppression, canon).
class RecoveryEffect : public Effect {
public:
  explicit RecoveryEffect(int denominator) : denominator_(denominator) {}
  void apply(EffectContext &ctx) const override;
  const char *name() const override { return "Recovery"; }

private:
  int denominator_;
};

// Roost's volatile: suppresses the user's Flying type until end of turn.
class RoostEffect : public Effect {
public:
  void apply(EffectContext &ctx) const override;
  const char *name() const override { return "Roost"; }
};

// Rest: full heal + self-inflicted 2-turn sleep (exempt from Sleep Clause,
// ADR #28). Cures any prior status. Fails at full HP or if already asleep.
class RestEffect : public Effect {
public:
  void apply(EffectContext &ctx) const override;
  const char *name() const override { return "Rest"; }
};

} // namespace engine
