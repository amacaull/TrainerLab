#include "engine/effects/set_screen.hpp"

#include "engine/core/battle_state.hpp"
#include "engine/core/events.hpp"
#include "engine/items/item.hpp"
#include "engine/model/move.hpp"

namespace engine {

void SetScreenEffect::apply(EffectContext &ctx) const {
  int side = ctx.user.side;
  if (ctx.state.weather != Weather::Snow ||
      ctx.state.aurora_veil_turns[static_cast<size_t>(side)] > 0) {
    ctx.events.emplace_back(MoveFailedEvent{ctx.user, ctx.move.name});
    ctx.moveFailed = true;
    return;
  }

  const BattlePokemon &setter =
      ctx.state.teams[static_cast<size_t>(side)][static_cast<size_t>(ctx.user.teamIndex)];
  int turns = 5;
  if (const Item *item = heldItem(setter))
    turns = item->screenDuration(turns);

  ctx.state.aurora_veil_turns[static_cast<size_t>(side)] = turns;
  ctx.events.emplace_back(ScreenStartedEvent{side, turns});
}

} // namespace engine
