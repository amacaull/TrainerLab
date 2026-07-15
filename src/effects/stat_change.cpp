#include "engine/effects/stat_change.hpp"

#include "engine/battle_state.hpp"

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

void StatChangeEffect::apply(EffectContext &ctx) const {
  CombatantRef ref = affectsUser_ ? ctx.user : ctx.target;
  BattlePokemon &mon =
      ctx.state.teams[static_cast<size_t>(ref.side)][static_cast<size_t>(ref.teamIndex)];
  if (mon.isFainted())
    return;

  applyStatStageDelta(mon, ref, stat_, delta_, ctx.events);
}

} // namespace engine
