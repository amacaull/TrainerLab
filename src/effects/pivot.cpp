#include "engine/effects/pivot.hpp"

#include "engine/core/battle_state.hpp"
#include "engine/core/switching.hpp"

namespace engine {

void PivotEffect::apply(EffectContext &ctx) const {
  int side = ctx.user.side;
  const BattlePokemon &user =
      ctx.state.teams[static_cast<size_t>(side)][static_cast<size_t>(ctx.user.teamIndex)];
  if (user.isFainted())
    return;

  int target = ctx.pivotTarget;
  if (!isValidSwitchTarget(ctx.state, side, target))
    target = firstHealthyBenched(ctx.state, side);
  if (target < 0)
    return;

  performSwitch(ctx.state, ctx.data, side, target, ctx.events);
}

} // namespace engine
