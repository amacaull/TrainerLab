#include "engine/effects/recoil.hpp"

#include "engine/abilities/ability.hpp"
#include "engine/items/item.hpp"

#include "engine/core/battle_state.hpp"

#include <algorithm>

namespace engine {

void RecoilEffect::apply(EffectContext &ctx) const {
  if (ctx.lastDamageDealt <= 0)
    return;
  BattlePokemon &user =
      ctx.state.teams[static_cast<size_t>(ctx.user.side)][static_cast<size_t>(ctx.user.teamIndex)];
  if (user.isFainted())
    return;

  // RockHead: recoil moves cost nothing (Struggle's own recoil is exempt).
  if (const Ability *ability = abilityOf(ctx.data, user)) {
    if (ability->blocksRecoil())
      return;
  }

  int hpBefore = user.currentHp;
  int damage = std::max(1, ctx.lastDamageDealt / denominator_);
  user.currentHp = std::max(0, user.currentHp - damage);
  ctx.events.emplace_back(RecoilDamageEvent{ctx.user, damage});
  if (user.isFainted()) {
    ctx.events.emplace_back(FaintedEvent{ctx.user});
  } else {
    itemHpCheck(ctx.state, ctx.data, ctx.user, ctx.events);
    abilityHpCheck(ctx.state, ctx.data, ctx.user, hpBefore, false, ctx.events);
  }
}

} // namespace engine
