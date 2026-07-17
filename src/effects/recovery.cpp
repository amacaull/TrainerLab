#include "engine/effects/recovery.hpp"

#include "engine/core/data_loader.hpp"
#include "engine/core/switching.hpp"

#include "engine/core/battle_state.hpp"
#include "engine/model/move.hpp"
#include "engine/model/status.hpp"

#include <algorithm>

namespace engine {

void RecoveryEffect::apply(EffectContext &ctx) const {
  BattlePokemon &user =
      ctx.state.teams[static_cast<size_t>(ctx.user.side)][static_cast<size_t>(ctx.user.teamIndex)];
  if (user.isFainted())
    return;
  if (user.currentHp >= user.stats.hp) {
    ctx.events.emplace_back(MoveFailedEvent{ctx.user, ctx.move.name});
    ctx.moveFailed = true;
    return;
  }
  int amount = std::min(std::max(1, user.stats.hp / denominator_), user.stats.hp - user.currentHp);
  user.currentHp += amount;
  ctx.events.emplace_back(HealedEvent{ctx.user, amount});
}

void RoostEffect::apply(EffectContext &ctx) const {
  BattlePokemon &user =
      ctx.state.teams[static_cast<size_t>(ctx.user.side)][static_cast<size_t>(ctx.user.teamIndex)];
  if (user.isFainted())
    return;
  user.roosted = 1;
}

void RestEffect::apply(EffectContext &ctx) const {
  BattlePokemon &user =
      ctx.state.teams[static_cast<size_t>(ctx.user.side)][static_cast<size_t>(ctx.user.teamIndex)];
  if (user.isFainted())
    return;
  // Electric Terrain also blocks the self-sleep of a grounded user (canon).
  bool terrainBlocks = ctx.state.terrain == Terrain::Electric &&
                       isGrounded(ctx.data.speciesByIndex(user.species_id));
  if (user.currentHp >= user.stats.hp || user.status == Status::Sleep || terrainBlocks) {
    ctx.events.emplace_back(MoveFailedEvent{ctx.user, ctx.move.name});
    ctx.moveFailed = true;
    return;
  }
  int amount = user.stats.hp - user.currentHp;
  user.currentHp = user.stats.hp;
  user.status = Status::Sleep;
  user.status_turns = 2;
  user.sleep_self_inflicted = 1;
  ctx.events.emplace_back(HealedEvent{ctx.user, amount});
  ctx.events.emplace_back(StatusAppliedEvent{ctx.user, Status::Sleep});
}

} // namespace engine
