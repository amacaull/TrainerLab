#pragma once

#include "engine/effects/effect.hpp"

#include <string>

namespace engine {

// Loops the shared damage core: 2-5 canon distribution or fixed count,
// escalating powers (TripleAxel), optional per-hit accuracy retest
// (PopulationBomb, TripleAxel). LoadedDice raises a 2-5 floor to 4 and
// makes counted moves land every hit (team rule).
class MultiHitEffect : public Effect {
public:
  void apply(EffectContext &ctx) const override;
  const char *name() const override { return "MultiHit"; }
};

// Heals the user for a fraction of the damage just dealt (BitterBlade,
// DrainPunch: half).
class DrainEffect : public Effect {
public:
  explicit DrainEffect(int denominator) : denominator_(denominator) {}
  void apply(EffectContext &ctx) const override;
  const char *name() const override { return "Drain"; }

private:
  int denominator_;
};

// SeismicToss (damage = user level) and Ruination (half the target's
// current HP). Type immunities from the chart still apply; stats and
// stages are ignored entirely.
class FixedDamageEffect : public Effect {
public:
  enum class Mode { Level, HalfCurrentHp };
  explicit FixedDamageEffect(Mode mode) : mode_(mode) {}
  void apply(EffectContext &ctx) const override;
  const char *name() const override { return "FixedDamage"; }

private:
  Mode mode_;
};

// SpectralThief: the target's positive stages move to the user (clamped),
// BEFORE the damage effect that follows in the chain.
class StealBoostsEffect : public Effect {
public:
  void apply(EffectContext &ctx) const override;
  const char *name() const override { return "StealBoosts"; }
};

// KnockOff's removal half: strips the target's held item for good. The
// x1.5 lives in the damage core (boostedByTargetItem).
class KnockOffEffect : public Effect {
public:
  void apply(EffectContext &ctx) const override;
  const char *name() const override { return "KnockOff"; }
};

// CeaselessEdge: a Spikes layer lands on the target's side on every hit
// that connects.
class HazardOnHitEffect : public Effect {
public:
  void apply(EffectContext &ctx) const override;
  const char *name() const override { return "HazardOnHit"; }
};

// BellyDrum: pay half the max HP, Attack jumps straight to +6. Fails
// below half HP or at +6 already. The payment can pop a SitrusBerry.
class BellyDrumEffect : public Effect {
public:
  void apply(EffectContext &ctx) const override;
  const char *name() const override { return "BellyDrum"; }
};

// SleepTalk: only while asleep (the move carries usableWhileAsleep);
// calls a random other move of the set, skipping accuracy, excluding
// two-turn moves and other sleep-only moves.
class SleepTalkEffect : public Effect {
public:
  void apply(EffectContext &ctx) const override;
  const char *name() const override { return "SleepTalk"; }
};

// DestinyBond: until the user's next action, a KO takes the killer along.
// Fails when chained (used twice in a row, canon gen 7+).
class DestinyBondEffect : public Effect {
public:
  void apply(EffectContext &ctx) const override;
  const char *name() const override { return "DestinyBond"; }
};

// Wish: at the end of the NEXT turn, whoever stands on this side heals
// half of the CASTER's max HP. Fails if a wish is already pending.
class WishEffect : public Effect {
public:
  void apply(EffectContext &ctx) const override;
  const char *name() const override { return "Wish"; }
};

} // namespace engine
