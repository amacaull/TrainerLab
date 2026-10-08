#include "engine/effects/stat_change.hpp"

#include "engine/abilities/ability.hpp"
#include "engine/core/battle_state.hpp"
#include "engine/core/data_loader.hpp"

#include <algorithm>

namespace engine {
void applyStatStageDelta(BattlePokemon &mon, CombatantRef ref, StatIndex stat, int delta,
                         EventLog &events) {
  int &stage = mon.stat_stages[static_cast<size_t>(stat)];
  int before = stage;
  stage = std::clamp(stage + delta, kMinStage, kMaxStage);

  int applied = stage - before;
  if (applied == 0) {
    events.emplace_back(StatChangeFailedEvent{ref, stat, delta > 0});
    return;
  }
  events.emplace_back(StatStageChangedEvent{ref, stat, applied});
}

void applyOpposingStatDrop(BattleState &state, const DataLoader &data, CombatantRef targetRef,
                           StatIndex stat, int delta, EventLog &events) {
  BattlePokemon &mon =
      state.teams[static_cast<size_t>(targetRef.side)][static_cast<size_t>(targetRef.teamIndex)];
  const Ability *ability = abilityOf(data, mon);
  if (ability && ability->blocksStatDrop()) {
    events.emplace_back(AbilityTriggeredEvent{targetRef, ability->name()});
    events.emplace_back(StatChangeFailedEvent{targetRef, stat, false});
    return;
  }
  int before = mon.stat_stages[static_cast<size_t>(stat)];
  applyStatStageDelta(mon, targetRef, stat, delta, events);
  bool lowered = mon.stat_stages[static_cast<size_t>(stat)] < before;
  if (lowered && ability) {
    AbilityContext actx{state, data, events, targetRef};
    ability->onStatLoweredByOpponent(actx);
  }
}

void StatChangeEffect::apply(EffectContext &ctx) const {
  CombatantRef ref = affectsUser_ ? ctx.user : ctx.target;
  BattlePokemon &mon =
      ctx.state.teams[static_cast<size_t>(ref.side)][static_cast<size_t>(ref.teamIndex)];
  if (mon.isFainted())
    return;

  if (!affectsUser_ && delta_ < 0 && ref.side != ctx.user.side) {
    applyOpposingStatDrop(ctx.state, ctx.data, ref, stat_, delta_, ctx.events);
    return;
  }

  applyStatStageDelta(mon, ref, stat_, delta_, ctx.events);
}
} // namespace engine
