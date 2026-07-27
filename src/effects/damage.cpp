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

void DamageEffect::apply(EffectContext &ctx) const {
  const Move &move = ctx.move;
  int power = ctx.powerOverride > 0 ? ctx.powerOverride : move.power;
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
  // Stat plumbing (phase 14): the category picks the pair unless the move
  // overrides it (BodyPress: own Def as offense; Psyshock: special vs Def),
  // and FoulPlay swings with the TARGET's Attack, stages included.
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

  // Crits: Showdown rates (base 1/24, high-crit moves 1/8), x1.5, and the
  // stages that would hurt the attacker are ignored (ADR #27).
  float critChance = move.highCrit ? (1.0f / 8.0f) : (1.0f / 24.0f);
  bool crit = ctx.rng.chance(critChance);

  int atkStage = offenseSrc.stat_stages[static_cast<size_t>(atkIdx)];
  int defStage = defender.stat_stages[static_cast<size_t>(defIdx)];
  if (crit) {
    atkStage = std::max(atkStage, 0);
    defStage = std::min(defStage, 0);
  }
  // Unaware ignores the other side's stages, both directions (canon).
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

  // Held items modify the stat itself, after stages (Choix x1.5, ThickClub x2).
  if (const Item *atkItem = heldItem(offenseSrc))
    effAtk *= atkItem->statMultiplier(atkIdx);
  if (const Item *defItem = heldItem(defender))
    effDef *= defItem->statMultiplier(defIdx);
  if (atkAbilityEarly)
    effAtk *= atkAbilityEarly->statMultiplier(atkIdx); // HugePower: Atk x2
  if (defAbilityEarly)
    effDef *= defAbilityEarly->statMultiplier(defIdx);

  // VesselOfRuin: the opponent's SpA is scaled while the holder stands.
  if (move.category == MoveCategory::Special && defAbilityEarly)
    effAtk *= defAbilityEarly->opposingSpAMultiplier();

  // Sandstorm: Rock types get SpD x1.5 (canon gen 4+); snow is the physical
  // mirror for Ice (ADR #37). Keyed on the RESOLVED defense stat so that
  // Psyshock (special vs Def) picks the right boost.
  if (ctx.state.weather == Weather::Sand && defIdx == StatIndex::SpD &&
      (defType1 == Type::Rock || defType2 == Type::Rock))
    effDef *= 1.5f;
  if (ctx.state.weather == Weather::Snow && defIdx == StatIndex::Def &&
      (defType1 == Type::Ice || defType2 == Type::Ice))
    effDef *= 1.5f;

  // Damage formula (gen 5+):
  //   base = floor( ((2*level/5 + 2) * power * Atk/Def) / 50 ) + 2
  // Then: STAB (x1.5), type effectiveness, random 0.85..1.0.
  int level = attacker.level;
  float base = (((2.0f * static_cast<float>(level) / 5.0f) + 2.0f) * static_cast<float>(power) *
                effAtk / effDef) /
                   50.0f +
               2.0f;

  // Struggle hits everything for neutral damage and never gets STAB (ADR #35).
  bool stab = !move.typeless && (move.type == attackerSp.type1 || move.type == attackerSp.type2);
  float stabValue = atkAbilityEarly ? atkAbilityEarly->stabMultiplier() : 1.5f;
  float stabMul = stab ? stabValue : 1.0f;

  float typeMul =
      move.typeless ? 1.0f : ctx.data.typeChart().effectiveness(move.type, defType1, defType2);

  // Souffle Delta neutralizes hits that are super effective against the
  // Flying component (ADR #47): that component's factor drops to x1.
  if (!move.typeless && ctx.state.weather == Weather::StrongWinds) {
    const TypeChart &chart = ctx.data.typeChart();
    for (Type defType : {defType1, defType2}) {
      if (defType == Type::Flying) {
        float flyingFactor = chart.effectiveness(move.type, Type::Flying, Type::Flying);
        if (flyingFactor > 1.0f)
          typeMul /= flyingFactor;
        break; // mono-typed conventions repeat the type: neutralize once
      }
    }
  }

  float randMul = static_cast<float>(ctx.rng.rangeInt(85, 100)) / 100.0f;

  const Ability *atkAbility = atkAbilityEarly;

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

  float abilityMul = atkAbility ? atkAbility->damageMultiplier(move, attacker) : 1.0f;

  const Ability *defAbility = abilityByName(defenderSp.ability);
  float defAbilityMul = defAbility ? defAbility->incomingDamageMultiplier(move) : 1.0f;

  float critMul = crit ? 1.5f : 1.0f;

  const Item *attackerItem = heldItem(attacker);
  float itemMul = attackerItem ? attackerItem->damageMultiplier() : 1.0f;
  // KnockOff hits x1.5 when there is something to knock off.
  if (move.boostedByTargetItem && heldItem(defender) != nullptr)
    itemMul *= 1.5f;

  // Electric Terrain boosts grounded attackers' Electric moves (gen 8+ value).
  float terrainMul = 1.0f;
  if (ctx.state.terrain == Terrain::Electric && move.type == Type::Electric &&
      isGrounded(attackerSp))
    terrainMul = 1.3f;

  // Voile Aurore halves both categories on the protected side; crits punch
  // through the screen (canon, ADR #39).
  float screenMul = 1.0f;
  if (ctx.state.aurora_veil_turns[static_cast<size_t>(ctx.target.side)] > 0 && !crit)
    screenMul = 0.5f;

  // Earthquake reaches a target hiding underground and hits twice as hard;
  // SolarBeam is halved by any non-sun active weather (canon, ADR #29).
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
    // Immunity stops the rest of the effect chain (Volt Switch vs Ground
    // doesn't pivot, Rapid Spin vs Ghost doesn't clear hazards).
    ctx.moveFailed = true;
  }

  // Disguise: the first direct hit pops the disguise instead — no
  // damage, no secondaries, a 1/8 max-HP chip (gen 8 rules, ADR #47).
  if (defAbilityEarly && defAbilityEarly->hasDisguise() && defender.disguise_broken == 0) {
    defender.disguise_broken = 1;
    ctx.events.emplace_back(AbilityTriggeredEvent{ctx.target, defAbilityEarly->name()});
    int chip = std::max(1, defender.stats.hp / 8);
    int hpBeforeChip = defender.currentHp;
    defender.currentHp = std::max(0, defender.currentHp - chip);
    ctx.events.emplace_back(ItemDamageEvent{ctx.target, defAbilityEarly->name(), chip});
    if (defender.isFainted())
      ctx.events.emplace_back(FaintedEvent{ctx.target});
    else
      abilityHpCheck(ctx.state, ctx.data, ctx.target, hpBeforeChip, false, ctx.events);
    if (ctx.multiHitIndex < 0)
      ctx.moveFailed = true; // single hit: chain stops. Multi-hit: the
                             // disguise eats this hit, the next ones land.
    return;
  }

  // FocusSash intercepts a lethal hit taken at full HP (ADR #34).
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

  // Canon: a damaging Fire move thaws a frozen target.
  if (damage > 0 && (move.type == Type::Fire || move.thawsUser) &&
      defender.status == Status::Freeze) {
    defender.status = Status::None;
    defender.status_turns = 0;
    ctx.events.emplace_back(StatusCuredEvent{ctx.target, Status::Freeze});
  }
}

} // namespace engine
