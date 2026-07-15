#pragma once

#include "engine/effect.hpp"
#include "engine/pokemon.hpp"

namespace engine {

// Clamped stage change + StatStageChanged/StatChangeFailed event emission.
// Shared by StatChangeEffect and abilities (Intimidate).
void applyStatStageDelta(BattlePokemon &mon, CombatantRef ref, StatIndex stat, int delta,
                         EventLog &events);

// Adjusts one stat stage on either the user or the target, clamped to
// [-6, +6]. Target defaults to the move's target; self-boosts (Swords Dance)
// set affectsUser. Emits StatStageChanged (or StatChangeFailed at the cap).
class StatChangeEffect : public Effect {
public:
  StatChangeEffect(StatIndex stat, int delta, bool affectsUser)
      : stat_(stat), delta_(delta), affectsUser_(affectsUser) {}
  void apply(EffectContext &ctx) const override;
  const char *name() const override { return "StatChange"; }

private:
  StatIndex stat_;
  int delta_;
  bool affectsUser_;
};

} // namespace engine
