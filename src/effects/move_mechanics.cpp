#include "engine/effects/move_mechanics.hpp"

#include "engine/abilities/ability.hpp"
#include "engine/core/battle_state.hpp"
#include "engine/core/data_loader.hpp"
#include "engine/core/rng.hpp"
#include "engine/effects/damage.hpp"
#include "engine/effects/stat_change.hpp"
#include "engine/items/item.hpp"
#include "engine/model/move.hpp"

#include <algorithm>

namespace engine {

namespace {

BattlePokemon &monAt(EffectContext &ctx, const CombatantRef &ref) {
  return ctx.state.teams[static_cast<size_t>(ref.side)][static_cast<size_t>(ref.teamIndex)];
}

bool holderHasLoadedDice(const EffectContext &ctx) {
  const BattlePokemon &user =
      ctx.state.teams[static_cast<size_t>(ctx.user.side)][static_cast<size_t>(ctx.user.teamIndex)];
  const Item *item = heldItem(user);
  return item != nullptr && std::string_view(item->name()) == "LoadedDice";
}

void failMove(EffectContext &ctx) {
  ctx.events.emplace_back(MoveFailedEvent{ctx.user, ctx.move.name});
  ctx.moveFailed = true;
}

} // namespace

void MultiHitEffect::apply(EffectContext &ctx) const {
  const Move &move = ctx.move;
  bool dice = holderHasLoadedDice(ctx);

  int hits;
  if (!move.hitPowers.empty()) {
    hits = static_cast<int>(move.hitPowers.size());
  } else if (move.minHits == move.maxHits) {
    hits = move.minHits; // fixed count (DragonDarts 2, PopulationBomb 10)
  } else {
    // Canon 2-5 distribution: 35/35/15/15. LoadedDice: floor raised to 4.
    if (dice) {
      hits = 4 + (ctx.rng.chance(0.5f) ? 1 : 0);
    } else {
      int roll = ctx.rng.rangeInt(1, 100);
      hits = roll <= 35 ? 2 : roll <= 70 ? 3 : roll <= 85 ? 4 : 5;
    }
  }

  DamageEffect damage;
  int landed = 0;
  for (int i = 0; i < hits; ++i) {
    BattlePokemon &defender = monAt(ctx, ctx.target);
    if (defender.isFainted())
      break;
    // Per-hit accuracy retest (PopulationBomb, TripleAxel). LoadedDice
    // guarantees every hit (team rule). The engine already rolled hit 1.
    if (move.perHitAccuracy && i > 0 && !dice) {
      if (!ctx.rng.chancePct(move.accuracy)) {
        ctx.events.emplace_back(MissedEvent{ctx.user, ctx.move.name});
        break; // canon: the volley stops on the first miss
      }
    }
    ctx.multiHitIndex = i;
    ctx.powerOverride = move.hitPowers.empty() ? 0 : move.hitPowers[static_cast<size_t>(i)];
    damage.apply(ctx);
    ctx.powerOverride = 0;
    if (ctx.moveFailed)
      break; // immunity: no further hits
    if (ctx.lastDamageDealt > 0)
      ++landed;
  }
  ctx.multiHitIndex = -1;
  if (landed == 0 && !ctx.moveFailed)
    ctx.moveFailed = true; // every hit blocked (Disguise alone): chain stops
}

void DrainEffect::apply(EffectContext &ctx) const {
  if (ctx.lastDamageDealt <= 0)
    return;
  BattlePokemon &user = monAt(ctx, ctx.user);
  if (user.isFainted())
    return;
  int healed =
      std::min(std::max(1, ctx.lastDamageDealt / denominator_), user.stats.hp - user.currentHp);
  if (healed <= 0)
    return;
  user.currentHp += healed;
  ctx.events.emplace_back(HealedEvent{ctx.user, healed});
}

void FixedDamageEffect::apply(EffectContext &ctx) const {
  BattlePokemon &attacker = monAt(ctx, ctx.user);
  BattlePokemon &defender = monAt(ctx, ctx.target);
  if (defender.isFainted()) {
    failMove(ctx);
    return;
  }
  const Species &defSp = ctx.data.speciesByIndex(defender.species_id);
  float typeMul = ctx.data.typeChart().effectiveness(ctx.move.type, defSp.type1, defSp.type2);
  if (typeMul == 0.0f) { // SeismicToss still bounces off Ghosts (canon)
    ctx.events.emplace_back(DamageDealtEvent{ctx.target, 0, 0.0f, false, false});
    ctx.moveFailed = true;
    return;
  }

  int damage = mode_ == Mode::Level ? attacker.level : std::max(1, defender.currentHp / 2);

  if (damage >= defender.currentHp) {
    if (const Item *defItem = heldItem(defender)) {
      ItemContext ictx{ctx.state, ctx.data, ctx.events, ctx.target};
      damage = defItem->adjustLethalDamage(ictx, damage);
    }
  }

  int hpBefore = defender.currentHp;
  defender.currentHp = std::max(0, defender.currentHp - damage);
  ctx.lastDamageDealt = hpBefore - defender.currentHp;
  ctx.events.emplace_back(DamageDealtEvent{ctx.target, damage, 1.0f, false, false});

  if (ctx.lastDamageDealt > 0) {
    itemHpCheck(ctx.state, ctx.data, ctx.target, ctx.events);
    abilityHpCheck(ctx.state, ctx.data, ctx.target, hpBefore, true, ctx.events);
  }

  if (defender.isFainted()) {
    ctx.events.emplace_back(FaintedEvent{ctx.target});
    if (defender.destiny_bond_active != 0 && !attacker.isFainted()) {
      attacker.currentHp = 0;
      ctx.events.emplace_back(DestinyBondTriggeredEvent{ctx.user});
      ctx.events.emplace_back(FaintedEvent{ctx.user});
    }
  }
}

void StealBoostsEffect::apply(EffectContext &ctx) const {
  BattlePokemon &user = monAt(ctx, ctx.user);
  BattlePokemon &target = monAt(ctx, ctx.target);
  if (target.isFainted())
    return;
  for (int i = 0; i < kStatStageCount; ++i) {
    int stolen = target.stat_stages[static_cast<size_t>(i)];
    if (stolen <= 0)
      continue;
    target.stat_stages[static_cast<size_t>(i)] = 0;
    applyStatStageDelta(user, ctx.user, static_cast<StatIndex>(i), stolen, ctx.events);
  }
}

void KnockOffEffect::apply(EffectContext &ctx) const {
  if (ctx.lastDamageDealt <= 0)
    return;
  BattlePokemon &target = monAt(ctx, ctx.target);
  const Item *item = heldItem(target);
  if (item == nullptr)
    return;
  ctx.events.emplace_back(ItemKnockedOffEvent{ctx.target, item->name()});
  target.item_id = kNoItem; // gone for the whole battle; the Choice lock dies with it
}

void HazardOnHitEffect::apply(EffectContext &ctx) const {
  if (ctx.lastDamageDealt <= 0)
    return;
  SideHazards &hz = ctx.state.hazards[static_cast<size_t>(ctx.target.side)];
  if (hz.spikes >= 3)
    return;
  hz.spikes += 1;
  ctx.events.emplace_back(HazardSetEvent{ctx.target.side, HazardKind::Spikes, hz.spikes});
}

void BellyDrumEffect::apply(EffectContext &ctx) const {
  BattlePokemon &user = monAt(ctx, ctx.user);
  int cost = user.stats.hp / 2;
  if (user.currentHp <= cost ||
      user.stat_stages[static_cast<size_t>(StatIndex::Atk)] >= kMaxStage) {
    failMove(ctx);
    return;
  }
  int hpBefore = user.currentHp;
  user.currentHp -= cost;
  ctx.events.emplace_back(RecoilDamageEvent{ctx.user, cost});
  int gain = kMaxStage - user.stat_stages[static_cast<size_t>(StatIndex::Atk)];
  applyStatStageDelta(user, ctx.user, StatIndex::Atk, gain, ctx.events);
  // The payment lands the user at (or below) half: the Sitrus combo (canon).
  itemHpCheck(ctx.state, ctx.data, ctx.user, ctx.events);
  abilityHpCheck(ctx.state, ctx.data, ctx.user, hpBefore, false, ctx.events);
}

void SleepTalkEffect::apply(EffectContext &ctx) const {
  BattlePokemon &user = monAt(ctx, ctx.user);
  if (user.status != Status::Sleep) {
    failMove(ctx);
    return;
  }
  int candidates[kMaxMovesPerPokemon];
  int n = 0;
  for (int i = 0; i < kMaxMovesPerPokemon; ++i) {
    int mid = user.move_ids[static_cast<size_t>(i)];
    if (mid == kNoMove)
      continue;
    const Move &m = ctx.data.moveByIndex(mid);
    if (m.usableWhileAsleep || m.twoTurn != TwoTurn::None)
      continue; // no SleepTalk-ception, no charge moves (canon-lite)
    candidates[n++] = mid;
  }
  if (n == 0) {
    failMove(ctx);
    return;
  }
  const Move &called = ctx.data.moveByIndex(candidates[ctx.rng.rangeInt(0, n - 1)]);
  ctx.events.emplace_back(MoveUsedEvent{ctx.user, called.name});
  // Called moves skip accuracy (canon). Re-enter the chain with a scoped
  // sub-context on the same state/events.
  EffectContext sub{ctx.state, ctx.data, ctx.rng, ctx.events, ctx.user, ctx.target, called, -1};
  for (const auto &effect : called.effects) {
    effect->apply(sub);
    if (sub.moveFailed)
      break;
  }
}

void DestinyBondEffect::apply(EffectContext &ctx) const {
  BattlePokemon &user = monAt(ctx, ctx.user);
  // Chained casts fail (canon gen 7+): last_move_id is set by the engine
  // after each executed move, so it already names this move on a repeat.
  if (user.last_move_id != kNoMove && &ctx.data.moveByIndex(user.last_move_id) == &ctx.move) {
    failMove(ctx);
    return;
  }
  user.destiny_bond_active = 1;
}

void WishEffect::apply(EffectContext &ctx) const {
  int side = ctx.user.side;
  if (ctx.state.wish_turns[static_cast<size_t>(side)] > 0) {
    failMove(ctx);
    return;
  }
  const BattlePokemon &user = monAt(ctx, ctx.user);
  ctx.state.wish_turns[static_cast<size_t>(side)] = 2; // heals at the end of next turn
  ctx.state.wish_heal[static_cast<size_t>(side)] = std::max(1, user.stats.hp / 2);
}

} // namespace engine
