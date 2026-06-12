#include "engine/effects/apply_status.hpp"

#include "engine/battle_state.hpp"
#include "engine/data_loader.hpp"
#include "engine/move.hpp"
#include "engine/pokemon.hpp"
#include "engine/rng.hpp"

namespace engine {

namespace {

// Canon type immunities: Fire can't burn, Electric can't be paralyzed,
// Poison/Steel can't be poisoned.
bool typeImmuneToStatus(Status s, const Species &sp) {
  auto hasType = [&sp](Type t) { return sp.type1 == t || sp.type2 == t; };
  switch (s) {
  case Status::Burn:
    return hasType(Type::Fire);
  case Status::Paralysis:
    return hasType(Type::Electric);
  case Status::Poison:
  case Status::Toxic:
    return hasType(Type::Poison) || hasType(Type::Steel);
  default:
    return false;
  }
}

// Sleep Clause: at most one sleeping (non-fainted) Pokemon per side.
// TODO phase 8: exempt self-inflicted sleep if Rest is added.
bool sideHasSleeper(const BattleState &state, int side) {
  int size = state.team_size[static_cast<size_t>(side)];
  const auto &team = state.teams[static_cast<size_t>(side)];
  for (int i = 0; i < size; ++i) {
    const BattlePokemon &p = team[static_cast<size_t>(i)];
    if (!p.isFainted() && p.status == Status::Sleep)
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

  if (chartImmune || target.status != Status::None || typeImmuneToStatus(status_, sp) ||
      (status_ == Status::Sleep && sideHasSleeper(ctx.state, ctx.target.side))) {
    ctx.events.emplace_back(StatusFailedEvent{ctx.target, status_});
    return;
  }

  target.status = status_;
  target.status_turns =
      (status_ == Status::Sleep) ? ctx.rng.rangeInt(kSleepMinTurns, kSleepMaxTurns) : 0;
  ctx.events.emplace_back(StatusAppliedEvent{ctx.target, status_});
}

} // namespace engine
