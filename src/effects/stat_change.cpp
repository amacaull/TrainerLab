#include "engine/effects/stat_change.hpp"

#include "engine/battle_state.hpp"

#include <algorithm>

namespace engine {

void StatChangeEffect::apply(EffectContext &ctx) const {
  CombatantRef ref = affectsUser_ ? ctx.user : ctx.target;
  BattlePokemon &mon =
      ctx.state.teams[static_cast<size_t>(ref.side)][static_cast<size_t>(ref.teamIndex)];
  if (mon.isFainted())
    return;

  int &stage = mon.stat_stages[static_cast<size_t>(stat_)];
  int before = stage;
  stage = std::clamp(stage + delta_, kMinStage, kMaxStage);

  int applied = stage - before;
  if (applied == 0) {
    ctx.events.emplace_back(StatChangeFailedEvent{ref, stat_, delta_ > 0});
    return;
  }
  ctx.events.emplace_back(StatStageChangedEvent{ref, stat_, applied});
}

} // namespace engine
