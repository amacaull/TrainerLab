#pragma once

#include "engine/effects/effect.hpp"
#include "engine/model/pokemon.hpp"

namespace engine {
void applyStatStageDelta(BattlePokemon &mon, CombatantRef ref, StatIndex stat, int delta,
                         EventLog &events);

// Every stat drop inflicted by an opponent must go through here: ClearBody and Defiant depend on
// it.
struct BattleState;
class DataLoader;
void applyOpposingStatDrop(BattleState &state, const DataLoader &data, CombatantRef targetRef,
                           StatIndex stat, int delta, EventLog &events);

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
