#include "engine/effects/protect.hpp"

#include "engine/core/battle_state.hpp"
#include "engine/core/rng.hpp"
#include "engine/model/move.hpp"

#include <cmath>

namespace engine {

void ProtectEffect::apply(EffectContext &ctx) const {
  BattlePokemon &user =
      ctx.state.teams[static_cast<size_t>(ctx.user.side)][static_cast<size_t>(ctx.user.teamIndex)];
  if (user.isFainted())
    return;

  float odds = 1.0f / std::pow(3.0f, static_cast<float>(user.protect_chain));
  if (user.protect_chain > 0 && !ctx.rng.chance(odds)) {
    ctx.events.emplace_back(MoveFailedEvent{ctx.user, ctx.move.name});
    ctx.moveFailed = true;
    return; // protected_now stays 0: the chain resets at end of turn
  }
  user.protected_now = 1;
  user.protect_chain += 1;
  ctx.events.emplace_back(ProtectedEvent{ctx.user});
}

} // namespace engine
