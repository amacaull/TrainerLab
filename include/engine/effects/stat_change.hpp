#pragma once

#include "engine/effects/effect.hpp"
#include "engine/model/pokemon.hpp"

namespace engine {

// Clamped stage change + StatStageChanged/StatChangeFailed event emission.
// Shared by StatChangeEffect and abilities (Intimidate).
void applyStatStageDelta(BattlePokemon &mon, CombatantRef ref, StatIndex stat, int delta,
                         EventLog &events);

// Every stat drop inflicted by an opponent routes through here: ClearBody
// blocks it, Defiant answers it with +2 Atk. Shared by StatChangeEffect
// and Intimidate.
struct BattleState;
class DataLoader;
void applyOpposingStatDrop(BattleState &state, const DataLoader &data, CombatantRef targetRef,
                           StatIndex stat, int delta, EventLog &events);

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
