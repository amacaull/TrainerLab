#include "engine/effects/force_switch.hpp"

#include "engine/battle_state.hpp"
#include "engine/move.hpp"
#include "engine/rng.hpp"
#include "engine/switching.hpp"

#include <array>

namespace engine {

void ForceSwitchEffect::apply(EffectContext &ctx) const {
  int side = ctx.target.side;
  const BattlePokemon &target =
      ctx.state.teams[static_cast<size_t>(side)][static_cast<size_t>(ctx.target.teamIndex)];
  if (target.isFainted())
    return; // KO'd by this very move: replacement flow takes over

  std::array<int, kTeamSize> candidates{};
  int count = 0;
  for (int i = 0; i < ctx.state.team_size[static_cast<size_t>(side)]; ++i) {
    if (isValidSwitchTarget(ctx.state, side, i))
      candidates[static_cast<size_t>(count++)] = i;
  }

  if (count == 0) {
    // Damage-only for Dragon Tail; a pure phazing move just fails.
    if (ctx.lastDamageDealt <= 0) {
      ctx.events.emplace_back(MoveFailedEvent{ctx.user, ctx.move.name});
      ctx.moveFailed = true;
    }
    return;
  }

  int pick = candidates[static_cast<size_t>(ctx.rng.rangeInt(0, count - 1))];
  performSwitch(ctx.state, ctx.data, side, pick, ctx.events);
}

} // namespace engine
