#include "engine/effects/damage.hpp"

#include "engine/battle_state.hpp"
#include "engine/data_loader.hpp"
#include "engine/move.hpp"
#include "engine/pokemon.hpp"
#include "engine/rng.hpp"
#include "engine/types.hpp"

#include <algorithm>
#include <cmath>

namespace engine {

void DamageEffect::apply(EffectContext &ctx) const {
  const Move &move = ctx.move;
  if (move.power <= 0)
    return;

  BattlePokemon &attacker =
      ctx.state.teams[static_cast<size_t>(ctx.user.side)][static_cast<size_t>(ctx.user.teamIndex)];
  BattlePokemon &defender =
      ctx.state
          .teams[static_cast<size_t>(ctx.target.side)][static_cast<size_t>(ctx.target.teamIndex)];

  const Species &attackerSp = ctx.data.speciesByIndex(attacker.species_id);
  const Species &defenderSp = ctx.data.speciesByIndex(defender.species_id);

  int atkStat, defStat;
  StatIndex atkIdx, defIdx;
  if (move.category == MoveCategory::Physical) {
    atkStat = attacker.stats.atk;
    defStat = defender.stats.def;
    atkIdx = StatIndex::Atk;
    defIdx = StatIndex::Def;
  } else if (move.category == MoveCategory::Special) {
    atkStat = attacker.stats.specAtk;
    defStat = defender.stats.specDef;
    atkIdx = StatIndex::SpA;
    defIdx = StatIndex::SpD;
  } else {
    return;
  }

  float atkMul = stageMultiplier(attacker.stat_stages[static_cast<size_t>(atkIdx)]);
  float defMul = stageMultiplier(defender.stat_stages[static_cast<size_t>(defIdx)]);
  float effAtk = static_cast<float>(atkStat) * atkMul;
  float effDef = static_cast<float>(defStat) * defMul;

  // Damage formula (gen 5+):
  //   base = floor( ((2*level/5 + 2) * power * Atk/Def) / 50 ) + 2
  // Then: STAB (x1.5), type effectiveness, random 0.85..1.0.
  int level = attacker.level;
  float base = (((2.0f * static_cast<float>(level) / 5.0f) + 2.0f) *
                static_cast<float>(move.power) * effAtk / effDef) /
                   50.0f +
               2.0f;

  bool stab = (move.type == attackerSp.type1 || move.type == attackerSp.type2);
  float stabMul = stab ? 1.5f : 1.0f;

  float typeMul = ctx.data.typeChart().effectiveness(move.type, defenderSp.type1, defenderSp.type2);

  float randMul = static_cast<float>(ctx.rng.rangeInt(85, 100)) / 100.0f;

  // Burn: final x0.5 modifier on physical damage (canon gen 5+).
  float burnMul =
      (attacker.status == Status::Burn && move.category == MoveCategory::Physical) ? 0.5f : 1.0f;

  float total = base * stabMul * typeMul * randMul * burnMul;
  int damage = std::max(1, static_cast<int>(std::floor(total)));
  if (typeMul == 0.0f)
    damage = 0;

  defender.currentHp = std::max(0, defender.currentHp - damage);

  ctx.events.emplace_back(DamageDealtEvent{ctx.target, damage, typeMul, stab});

  if (defender.isFainted()) {
    ctx.events.emplace_back(FaintedEvent{ctx.target});
    return;
  }

  // Canon: a damaging Fire move thaws a frozen target.
  if (damage > 0 && move.type == Type::Fire && defender.status == Status::Freeze) {
    defender.status = Status::None;
    defender.status_turns = 0;
    ctx.events.emplace_back(StatusCuredEvent{ctx.target, Status::Freeze});
  }
}

} // namespace engine
