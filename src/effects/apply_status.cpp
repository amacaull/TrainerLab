#include "engine/effects/apply_status.hpp"

#include "engine/abilities/ability.hpp"
#include "engine/core/switching.hpp"

#include "engine/core/battle_state.hpp"
#include "engine/core/data_loader.hpp"
#include "engine/core/rng.hpp"
#include "engine/model/move.hpp"
#include "engine/model/pokemon.hpp"

namespace engine {

namespace {

// Sleep Clause: at most one sleeping (non-fainted) Pokemon per side.
// Rest sleep is exempt: only opponent-induced sleep counts (ADR #28).
bool sideHasSleeper(const BattleState &state, int side) {
  int size = state.team_size[static_cast<size_t>(side)];
  const auto &team = state.teams[static_cast<size_t>(side)];
  for (int i = 0; i < size; ++i) {
    const BattlePokemon &p = team[static_cast<size_t>(i)];
    if (!p.isFainted() && p.status == Status::Sleep && p.sleep_self_inflicted == 0)
      return true;
  }
  return false;
}

} // namespace

void ApplyStatusEffect::apply(EffectContext &ctx) const {
  BattlePokemon &target =
      ctx.state
          .teams[static_cast<size_t>(ctx.target.side)][static_cast<size_t>(ctx.target.teamIndex)];
  if (target.isFainted())
    return;

  const Species &sp = ctx.data.speciesByIndex(target.species_id);

  // Type-chart immunity also blocks status moves (ThunderWave vs Ground).
  bool chartImmune = ctx.data.typeChart().effectiveness(ctx.move.type, sp.type1, sp.type2) == 0.0f;

  // Canon: nothing can be frozen under harsh sunlight.
  bool sunBlocksFreeze = (status_ == Status::Freeze && ctx.state.weather == Weather::Sun);

  // Electric Terrain keeps grounded Pokemon awake (ADR #38).
  bool terrainBlocksSleep =
      (status_ == Status::Sleep && ctx.state.terrain == Terrain::Electric && isGrounded(sp));

  // LeafGuard: no status at all under the sun.
  const Ability *targetAbility = abilityOf(ctx.data, target);
  bool abilityBlocks = targetAbility && targetAbility->blocksStatus(ctx.state);

  if (chartImmune || sunBlocksFreeze || terrainBlocksSleep || abilityBlocks ||
      target.status != Status::None || typeImmuneToStatus(status_, sp) ||
      (status_ == Status::Sleep && sideHasSleeper(ctx.state, ctx.target.side))) {
    ctx.events.emplace_back(StatusFailedEvent{ctx.target, status_});
    return;
  }

  target.status = status_;
  target.status_turns =
      (status_ == Status::Sleep) ? ctx.rng.rangeInt(kSleepMinTurns, kSleepMaxTurns) : 0;
  target.sleep_self_inflicted = 0;
  ctx.events.emplace_back(StatusAppliedEvent{ctx.target, status_});
}

} // namespace engine
