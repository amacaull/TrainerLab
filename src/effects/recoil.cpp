#include "engine/effects/recoil.hpp"

#include "engine/item.hpp"

#include "engine/battle_state.hpp"

#include <algorithm>

namespace engine {

void RecoilEffect::apply(EffectContext &ctx) const {
  if (ctx.lastDamageDealt <= 0)
    return;
  BattlePokemon &user =
      ctx.state.teams[static_cast<size_t>(ctx.user.side)][static_cast<size_t>(ctx.user.teamIndex)];
  if (user.isFainted())
    return;

  int damage = std::max(1, ctx.lastDamageDealt / denominator_);
  user.currentHp = std::max(0, user.currentHp - damage);
  ctx.events.emplace_back(RecoilDamageEvent{ctx.user, damage});
  if (user.isFainted())
    ctx.events.emplace_back(FaintedEvent{ctx.user});
  else
    itemHpCheck(ctx.state, ctx.data, ctx.user, ctx.events);
}

} // namespace engine
