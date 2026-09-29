#include "engine/effects/damage.hpp"

#include "engine/abilities/ability.hpp"
#include "engine/core/battle_state.hpp"
#include "engine/core/data_loader.hpp"
#include "engine/core/rng.hpp"
#include "engine/core/switching.hpp"
#include "engine/items/item.hpp"
#include "engine/model/move.hpp"
#include "engine/model/pokemon.hpp"
#include "engine/model/types.hpp"

#include <algorithm>
#include <cmath>

namespace engine {
bool absorbedByDisguise(EffectContext &ctx) {
  BattlePokemon &defender =
      ctx.state
          .teams[static_cast<size_t>(ctx.target.side)][static_cast<size_t>(ctx.target.teamIndex)];
  const Ability *ability = abilityOf(ctx.data, defender);
  if (!ability || !ability->hasDisguise() || defender.disguise_broken != 0)
    return false;
  defender.disguise_broken = 1;
  ctx.events.emplace_back(AbilityTriggeredEvent{ctx.target, ability->name()});
  int chip = std::max(1, defender.stats.hp / 8);
  int hpBeforeChip = defender.currentHp;
  defender.currentHp = std::max(0, defender.currentHp - chip);
  ctx.events.emplace_back(AbilityDamageEvent{ctx.target, ability->name(), chip});
  if (defender.isFainted())
    ctx.events.emplace_back(FaintedEvent{ctx.target});
  else
    abilityHpCheck(ctx.state, ctx.data, ctx.target, hpBeforeChip, false, ctx.events);
  if (ctx.multiHitIndex < 0)
    ctx.moveFailed = true;
  return true;
}

void DamageEffect::apply(EffectContext &ctx) const {
  const Move &move = ctx.move;
  int power = ctx.powerOverride > 0 ? ctx.powerOverride : move.power;
  if (move.powerFromTargetWeight) {
    const BattlePokemon &weighed =
        ctx.state
            .teams[static_cast<size_t>(ctx.target.side)][static_cast<size_t>(ctx.target.teamIndex)];
    double kg = ctx.data.speciesByIndex(weighed.species_id).weightKg;
    power = kg < 10.0    ? 20
            : kg < 25.0  ? 40
            : kg < 50.0  ? 60
            : kg < 100.0 ? 80
            : kg < 200.0 ? 100
                         : 120;
  }
  if (power <= 0)
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

  if (move.category == MoveCategory::Status)
    return;
  StatIndex atkIdx = (move.category == MoveCategory::Physical) ? StatIndex::Atk : StatIndex::SpA;
  StatIndex defIdx = (move.category == MoveCategory::Physical) ? StatIndex::Def : StatIndex::SpD;
  if (move.offenseStat != StatIndex::Count)
    atkIdx = move.offenseStat;
  if (move.defenseStat != StatIndex::Count)
    defIdx = move.defenseStat;
  const BattlePokemon &offenseSrc = move.useTargetOffense ? defender : attacker;
  auto statByIndex = [](const Stats &st, StatIndex idx) {
    switch (idx) {
    case StatIndex::Atk:
      return st.atk;
    case StatIndex::Def:
      return st.def;
    case StatIndex::SpA:
      return st.specAtk;
    case StatIndex::SpD:
      return st.specDef;
    default:
      return st.speed;
    }
  };
  int atkStat = statByIndex(offenseSrc.stats, atkIdx);
  int defStat = statByIndex(defender.stats, defIdx);

  // Showdown crit rates. A crit ignores the stages that would hurt the attacker (canon).
  float critChance = move.highCrit ? (1.0f / 8.0f) : (1.0f / 24.0f);
  bool crit = ctx.rng.chance(critChance);

  int atkStage = offenseSrc.stat_stages[static_cast<size_t>(atkIdx)];
  int defStage = defender.stat_stages[static_cast<size_t>(defIdx)];
  if (crit) {
    atkStage = std::max(atkStage, 0);
    defStage = std::min(defStage, 0);
  }
  const Ability *atkAbilityEarly = abilityOf(ctx.data, attacker);
  const Ability *defAbilityEarly = abilityOf(ctx.data, defender);
  if (defAbilityEarly && defAbilityEarly->ignoresStages())
    atkStage = 0;
  if (atkAbilityEarly && atkAbilityEarly->ignoresStages())
    defStage = 0;
  float atkMul = stageMultiplier(atkStage);
  float defMul = stageMultiplier(defStage);
  float effAtk = static_cast<float>(atkStat) * atkMul;
  float effDef = static_cast<float>(defStat) * defMul;

  // Always the attacker's item: FoulPlay borrows the target's Attack, not its Choice Band (canon).
  if (const Item *atkItem = heldItem(attacker))
    effAtk *= atkItem->statMultiplier(atkIdx);
  if (const Item *defItem = heldItem(defender))
    effDef *= defItem->statMultiplier(defIdx);
  if (atkAbilityEarly)
    effAtk *= atkAbilityEarly->statMultiplier(atkIdx);
  if (defAbilityEarly)
    effDef *= defAbilityEarly->statMultiplier(defIdx);

  if (move.category == MoveCategory::Special && defAbilityEarly)
    effAtk *= defAbilityEarly->opposingSpAMultiplier();

  // Keyed on the resolved defense stat, so Psyshock (special vs Def) gets the right boost.
  if (ctx.state.weather == Weather::Sand && defIdx == StatIndex::SpD &&
      (defType1 == Type::Rock || defType2 == Type::Rock))
    effDef *= 1.5f;
  if (ctx.state.weather == Weather::Snow && defIdx == StatIndex::Def &&
      (defType1 == Type::Ice || defType2 == Type::Ice))
    effDef *= 1.5f;

  int level = attacker.level;
  float base = (((2.0f * static_cast<float>(level) / 5.0f) + 2.0f) * static_cast<float>(power) *
                effAtk / effDef) /
                   50.0f +
               2.0f;

  bool stab = !move.typeless && (move.type == attackerSp.type1 || move.type == attackerSp.type2);
  float stabValue = atkAbilityEarly ? atkAbilityEarly->stabMultiplier() : 1.5f;
  float stabMul = stab ? stabValue : 1.0f;

  float typeMul =
      move.typeless ? 1.0f : ctx.data.typeChart().effectiveness(move.type, defType1, defType2);

  // Delta Stream neutralizes hits that are super effective against the Flying component (canon).
  if (!move.typeless && ctx.state.weather == Weather::StrongWinds) {
    const TypeChart &chart = ctx.data.typeChart();
    for (Type defType : {defType1, defType2}) {
      if (defType == Type::Flying) {
        float flyingFactor = chart.effectiveness(move.type, Type::Flying, Type::Flying);
        if (flyingFactor > 1.0f)
          typeMul /= flyingFactor;
        break; // mono-typed species repeat the type: neutralize once
      }
    }
  }

  float randMul = static_cast<float>(ctx.rng.rangeInt(85, 100)) / 100.0f;

  const Ability *atkAbility = atkAbilityEarly;

  bool burnApplies = attacker.status == Status::Burn && move.category == MoveCategory::Physical &&
                     !(atkAbility && atkAbility->ignoresBurnPenalty());
  float burnMul = burnApplies ? 0.5f : 1.0f;

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

  float abilityMul = atkAbility ? atkAbility->damageMultiplier(move, attacker) : 1.0f;

  const Ability *defAbility = abilityByName(defenderSp.ability);
  float defAbilityMul = defAbility ? defAbility->incomingDamageMultiplier(move) : 1.0f;

  float critMul = crit ? 1.5f : 1.0f;

  const Item *attackerItem = heldItem(attacker);
  float itemMul = attackerItem ? attackerItem->damageMultiplier() : 1.0f;
  if (move.boostedByTargetItem && heldItem(defender) != nullptr)
    itemMul *= 1.5f;

  float terrainMul = 1.0f;
  if (ctx.state.terrain == Terrain::Electric && move.type == Type::Electric &&
      isGrounded(attackerSp))
    terrainMul = 1.3f;

  // A crit ignores Aurora Veil (canon).
  float screenMul = 1.0f;
  if (ctx.state.aurora_veil_turns[static_cast<size_t>(ctx.target.side)] > 0 && !crit)
    screenMul = 0.5f;

  float situationMul = 1.0f;
  if (defender.invulnerable_state == 2 && move.hitsDig)
    situationMul *= 2.0f;
  if (move.solarCharge && ctx.state.weather != Weather::None && ctx.state.weather != Weather::Sun)
    situationMul *= 0.5f;

  float total = base * stabMul * typeMul * randMul * burnMul * abilityMul * defAbilityMul *
                critMul * situationMul * weatherMul * itemMul * terrainMul * screenMul;
  int damage = std::max(1, static_cast<int>(std::floor(total)));
  if (typeMul == 0.0f) {
    damage = 0;
    // An immune hit stops the chain: Volt Switch into a Ground-type does not pivot.
    ctx.moveFailed = true;
  }

  // An immune hit never reaches Disguise.
  if (typeMul != 0.0f && absorbedByDisguise(ctx))
    return;

  if (damage >= defender.currentHp) {
    if (const Item *defItem = heldItem(defender)) {
      ItemContext ictx{ctx.state, ctx.data, ctx.events, ctx.target};
      damage = defItem->adjustLethalDamage(ictx, damage);
    }
  }

  int hpBefore = defender.currentHp;
  defender.currentHp = std::max(0, defender.currentHp - damage);
  ctx.lastDamageDealt = hpBefore - defender.currentHp;

  ctx.events.emplace_back(DamageDealtEvent{ctx.target, damage, typeMul, stab, crit});

  if (ctx.lastDamageDealt > 0) {
    itemHpCheck(ctx.state, ctx.data, ctx.target, ctx.events);
    abilityHpCheck(ctx.state, ctx.data, ctx.target, hpBefore, true, ctx.events);
  }

  // Contact punishment fires even if the holder goes down (canon).
  if (ctx.lastDamageDealt > 0) {
    if (const Ability *hitAbility = abilityByName(defenderSp.ability))
      hitAbility->onDamagingHit(ctx, ctx.target, ctx.user, move.makesContact);
  }

  if (defender.isFainted()) {
    ctx.events.emplace_back(FaintedEvent{ctx.target});
    // DestinyBond drags the killer along (direct move damage only, canon).
    if (defender.destiny_bond_active != 0 && !attacker.isFainted()) {
      attacker.currentHp = 0;
      ctx.events.emplace_back(DestinyBondTriggeredEvent{ctx.user});
      ctx.events.emplace_back(FaintedEvent{ctx.user});
    }
    return;
  }

  // EmergencyExit may have pulled the target out: nothing below concerns it any more.
  if (!stillOnField(ctx.state, ctx.target.side, ctx.target.teamIndex))
    return;

  if (damage > 0 && (move.type == Type::Fire || move.thawsUser) &&
      defender.status == Status::Freeze) {
    defender.status = Status::None;
    defender.status_turns = 0;
    ctx.events.emplace_back(StatusCuredEvent{ctx.target, Status::Freeze});
  }
}
} // namespace engine
