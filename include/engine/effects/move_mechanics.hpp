#pragma once

#include "engine/effects/effect.hpp"

#include <string>

namespace engine {
class MultiHitEffect : public Effect {
public:
  void apply(EffectContext &ctx) const override;
  const char *name() const override { return "MultiHit"; }
};

class DrainEffect : public Effect {
public:
  explicit DrainEffect(int denominator) : denominator_(denominator) {}
  void apply(EffectContext &ctx) const override;
  const char *name() const override { return "Drain"; }

private:
  int denominator_;
};

// Type immunities still apply; stats and stages are ignored.
class FixedDamageEffect : public Effect {
public:
  enum class Mode { Level, HalfCurrentHp };
  explicit FixedDamageEffect(Mode mode) : mode_(mode) {}
  void apply(EffectContext &ctx) const override;
  const char *name() const override { return "FixedDamage"; }

private:
  Mode mode_;
};

// Must run BEFORE the damage effect that follows it in the chain.
class StealBoostsEffect : public Effect {
public:
  void apply(EffectContext &ctx) const override;
  const char *name() const override { return "StealBoosts"; }
};

class KnockOffEffect : public Effect {
public:
  void apply(EffectContext &ctx) const override;
  const char *name() const override { return "KnockOff"; }
};

class HazardOnHitEffect : public Effect {
public:
  void apply(EffectContext &ctx) const override;
  const char *name() const override { return "HazardOnHit"; }
};

class BellyDrumEffect : public Effect {
public:
  void apply(EffectContext &ctx) const override;
  const char *name() const override { return "BellyDrum"; }
};

class SleepTalkEffect : public Effect {
public:
  void apply(EffectContext &ctx) const override;
  const char *name() const override { return "SleepTalk"; }
};

// Fails when used twice in a row (canon gen 7+).
class DestinyBondEffect : public Effect {
public:
  void apply(EffectContext &ctx) const override;
  const char *name() const override { return "DestinyBond"; }
};

// Heals whoever stands on the side at the end of the NEXT turn, for half the CASTER's max HP
// (canon).
class WishEffect : public Effect {
public:
  void apply(EffectContext &ctx) const override;
  const char *name() const override { return "Wish"; }
};
} // namespace engine
