#include "engine/effects/damage.hpp"

#include "engine/ability.hpp"
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
  // Canon: a move whose target already fainted mid-turn simply fails
  // (and a failed pivot doesn't switch).
  if (defender.isFainted()) {
    ctx.events.emplace_back(MoveFailedEvent{ctx.user, move.name});
    ctx.moveFailed = true;
    return;
  }

  const Species &defenderSp = ctx.data.speciesByIndex(defender.species_id);

  // Roost suppresses the defender's Flying type until end of turn
  // (pure Flying becomes Normal, canon).
  Type defType1 = defenderSp.type1;
  Type defType2 = defenderSp.type2;
  if (defender.roosted != 0) {
    if (defType1 == Type::Flying && defType2 == Type::Flying)
      defType1 = defType2 = Type::Normal;
    else if (defType1 == Type::Flying)
      defType1 = defType2;
    else if (defType2 == Type::Flying)
      defType2 = defType1;
  }

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

  // Crits: Showdown rates (base 1/24, high-crit moves 1/8), x1.5, and the
  // stages that would hurt the attacker are ignored (ADR #27).
  float critChance = move.highCrit ? (1.0f / 8.0f) : (1.0f / 24.0f);
  bool crit = ctx.rng.chance(critChance);

  int atkStage = attacker.stat_stages[static_cast<size_t>(atkIdx)];
  int defStage = defender.stat_stages[static_cast<size_t>(defIdx)];
  if (crit) {
    atkStage = std::max(atkStage, 0);
    defStage = std::min(defStage, 0);
  }
  float atkMul = stageMultiplier(atkStage);
  float defMul = stageMultiplier(defStage);
  float effAtk = static_cast<float>(atkStat) * atkMul;
  float effDef = static_cast<float>(defStat) * defMul;

  // Sandstorm: Rock types get SpD x1.5 (canon gen 4+, kept by Showdown).
  if (ctx.state.weather == Weather::Sand && move.category == MoveCategory::Special &&
      (defType1 == Type::Rock || defType2 == Type::Rock))
    effDef *= 1.5f;

  // Damage formula (gen 5+):
  //   base = floor( ((2*level/5 + 2) * power * Atk/Def) / 50 ) + 2
  // Then: STAB (x1.5), type effectiveness, random 0.85..1.0.
  int level = attacker.level;
  float base = (((2.0f * static_cast<float>(level) / 5.0f) + 2.0f) *
                static_cast<float>(move.power) * effAtk / effDef) /
                   50.0f +
               2.0f;

  // Lutte hits everything for neutral damage and never gets STAB (ADR #35).
  bool stab = !move.typeless && (move.type == attackerSp.type1 || move.type == attackerSp.type2);
  float stabMul = stab ? 1.5f : 1.0f;

  float typeMul =
      move.typeless ? 1.0f : ctx.data.typeChart().effectiveness(move.type, defType1, defType2);

  float randMul = static_cast<float>(ctx.rng.rangeInt(85, 100)) / 100.0f;

  // Attacker ability first: Guts may waive the burn penalty.
  const Ability *atkAbility = abilityByName(attackerSp.ability);

  // Burn: final x0.5 modifier on physical damage (canon gen 5+).
  bool burnApplies = attacker.status == Status::Burn && move.category == MoveCategory::Physical &&
                     !(atkAbility && atkAbility->ignoresBurnPenalty());
  float burnMul = burnApplies ? 0.5f : 1.0f;

  // Weather: rain boosts Water x1.5 and halves Fire; sun is the mirror.
  float weatherMul = 1.0f;
  if (ctx.state.weather == Weather::Rain) {
    if (move.type == Type::Water)
      weatherMul = 1.5f;
    else if (move.type == Type::Fire)
      weatherMul = 0.5f;
  } else if (ctx.state.weather == Weather::Sun) {
    if (move.type == Type::Fire)
      weatherMul = 1.5f;
    else if (move.type == Type::Water)
      weatherMul = 0.5f;
  }

  // Attacker ability (Blaze/Torrent/Overgrow/Swarm pinch, Guts).
  float abilityMul = atkAbility ? atkAbility->damageMultiplier(move, attacker) : 1.0f;

  // Defender ability (Thick Fat halves Fire/Ice).
  const Ability *defAbility = abilityByName(defenderSp.ability);
  float defAbilityMul = defAbility ? defAbility->incomingDamageMultiplier(move) : 1.0f;

  float critMul = crit ? 1.5f : 1.0f;

  // Earthquake reaches a target hiding underground and hits twice as hard;
  // SolarBeam is halved by any non-sun active weather (canon, ADR #29).
  float situationMul = 1.0f;
  if (defender.invulnerable_state == 2 && move.hitsDig)
    situationMul *= 2.0f;
  if (move.solarCharge && ctx.state.weather != Weather::None && ctx.state.weather != Weather::Sun)
    situationMul *= 0.5f;

  float total = base * stabMul * typeMul * randMul * burnMul * abilityMul * defAbilityMul *
                critMul * situationMul * weatherMul;
  int damage = std::max(1, static_cast<int>(std::floor(total)));
  if (typeMul == 0.0f) {
    damage = 0;
    // Immunity stops the rest of the effect chain (Volt Switch vs Ground
    // doesn't pivot, Rapid Spin vs Ghost doesn't clear hazards).
    ctx.moveFailed = true;
  }

  int hpBefore = defender.currentHp;
  defender.currentHp = std::max(0, defender.currentHp - damage);
  ctx.lastDamageDealt = hpBefore - defender.currentHp;

  ctx.events.emplace_back(DamageDealtEvent{ctx.target, damage, typeMul, stab, crit});

  // Contact punishment fires even if the holder goes down (canon).
  if (ctx.lastDamageDealt > 0) {
    if (const Ability *hitAbility = abilityByName(defenderSp.ability))
      hitAbility->onDamagingHit(ctx, ctx.target, ctx.user, move.makesContact);
  }

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
