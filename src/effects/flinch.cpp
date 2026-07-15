#include "engine/effects/flinch.hpp"

#include "engine/battle_state.hpp"

namespace engine {

void FlinchEffect::apply(EffectContext &ctx) const {
  BattlePokemon &target =
      ctx.state
          .teams[static_cast<size_t>(ctx.target.side)][static_cast<size_t>(ctx.target.teamIndex)];
  if (target.isFainted())
    return;
  target.flinched = 1;
}

} // namespace engine
