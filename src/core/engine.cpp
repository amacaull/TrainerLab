#include "engine/core/engine.hpp"

#include "engine/abilities/ability.hpp"
#include "engine/core/struggle.hpp"
#include "engine/core/switching.hpp"
#include "engine/effects/effect.hpp"
#include "engine/items/item.hpp"
#include "engine/model/move.hpp"
#include "engine/model/pokemon.hpp"
#include "engine/model/status.hpp"
#include "engine/model/types.hpp"

#include <algorithm>
#include <stdexcept>
#include <string>
#include <variant>

namespace engine {

namespace {

// The Choice lock only binds while the choice item is still in hand: a
// consumed or knocked-off item releases it (canon).
bool choiceLockActive(const BattlePokemon &p) {
  if (p.locked_move_id == kNoMove)
    return false;
  const Item *item = heldItem(p);
  return item != nullptr && item->locksMove();
}

// Lutte fallback test (ADR #35), lock-aware: a locked Pokemon whose locked
// slot ran dry can only Struggle, whatever the other slots hold.
bool canUseAnyMove(const BattlePokemon &p) {
  if (!choiceLockActive(p))
    return p.hasUsablePp();
  for (int i = 0; i < kMaxMovesPerPokemon; ++i)
    if (p.move_ids[static_cast<size_t>(i)] == p.locked_move_id && p.pp[static_cast<size_t>(i)] > 0)
      return true;
  return false;
}

// Switch actions take priority over moves (+6 brackets above any move).
int actionPriority(const Action &a, const DataLoader &data, const BattlePokemon &user) {
  if (std::holds_alternative<SwitchAction>(a))
    return 6;
  if (!canUseAnyMove(user))
    return 0; // will be substituted with Lutte (ADR #35)
  const auto &useMove = std::get<UseMove>(a);
  int moveId = user.move_ids[static_cast<size_t>(useMove.moveIndex)];
  return data.moveByIndex(moveId).priority;
}

int effectiveSpeed(const BattlePokemon &p) {
  float mul = stageMultiplier(p.stat_stages[static_cast<size_t>(StatIndex::Spe)]);
  if (const Item *item = heldItem(p)) // MouchoirChoix x1.5: changes turn order
    mul *= item->statMultiplier(StatIndex::Spe);
  int spd = static_cast<int>(static_cast<float>(p.stats.speed) * mul);
  if (p.status == Status::Paralysis)
    spd /= 2;
  return spd;
}

// Speed ties are broken randomly (ADR #31).
int fasterSide(const BattleState &state, RNG &rng) {
  int s0 = effectiveSpeed(state.active(0));
  int s1 = effectiveSpeed(state.active(1));
  if (s0 != s1)
    return (s0 > s1) ? 0 : 1;
  return rng.chance(0.5f) ? 0 : 1;
}

// Sleep/freeze/paralysis gate. Returns true if the user can act this turn;
// handles the wake-up and thaw transitions (mutates status counters).
bool passesBeforeMove(BattlePokemon &user, const CombatantRef &ref, RNG &rng, EventLog &events) {
  if (user.flinched != 0) {
    user.flinched = 0;
    events.emplace_back(MoveSkippedEvent{ref, SkipReason::Flinched});
    return false;
  }
  switch (user.status) {
  case Status::Sleep:
    if (user.status_turns > 0) {
      user.status_turns -= 1;
      events.emplace_back(MoveSkippedEvent{ref, SkipReason::Asleep});
      return false;
    }
    user.status = Status::None;
    user.sleep_self_inflicted = 0;
    events.emplace_back(StatusCuredEvent{ref, Status::Sleep});
    return true;
  case Status::Freeze:
    if (rng.chance(kThawChance)) {
      user.status = Status::None;
      events.emplace_back(StatusCuredEvent{ref, Status::Freeze});
      return true;
    }
    events.emplace_back(MoveSkippedEvent{ref, SkipReason::Frozen});
    return false;
  case Status::Paralysis:
    if (rng.chance(kFullParalysisChance)) {
      events.emplace_back(MoveSkippedEvent{ref, SkipReason::FullyParalyzed});
      return false;
    }
    return true;
  default:
    return true;
  }
}

// Sand chips everything but Rock/Ground/Steel; hail everything but Ice.
void applyWeatherChip(BattleState &state, const DataLoader &data, int side, EventLog &events) {
  BattlePokemon &p = state.active(side);
  if (p.isFainted())
    return;

  const Species &sp = data.speciesByIndex(p.species_id);
  auto hasType = [&sp](Type t) { return sp.type1 == t || sp.type2 == t; };
  if (state.weather == Weather::Sand &&
      (hasType(Type::Rock) || hasType(Type::Ground) || hasType(Type::Steel)))
    return;
  if (state.weather == Weather::Hail && hasType(Type::Ice))
    return;

  int damage = std::max(1, p.stats.hp / kWeatherChipDenom);
  p.currentHp = std::max(0, p.currentHp - damage);
  CombatantRef ref{side, state.activeIndex[static_cast<size_t>(side)]};
  events.emplace_back(WeatherDamageEvent{ref, state.weather, damage});
  if (p.isFainted())
    events.emplace_back(FaintedEvent{ref});
  else
    itemHpCheck(state, data, ref, events);
}

void applyResidual(BattleState &state, const DataLoader &data, int side, EventLog &events) {
  BattlePokemon &p = state.active(side);
  if (p.isFainted())
    return;

  int maxHp = p.stats.hp;
  int damage = 0;
  switch (p.status) {
  case Status::Burn:
    damage = std::max(1, maxHp / kBurnDamageDenom);
    break;
  case Status::Poison:
    damage = std::max(1, maxHp / kPoisonDamageDenom);
    break;
  case Status::Toxic:
    p.status_turns += 1; // ramps: n/16 on the n-th turn under Toxic
    damage = std::max(1, maxHp * p.status_turns / kToxicDamageDenom);
    break;
  default:
    return;
  }

  p.currentHp = std::max(0, p.currentHp - damage);
  CombatantRef ref{side, state.activeIndex[static_cast<size_t>(side)]};
  events.emplace_back(StatusDamageEvent{ref, p.status, damage});
  if (p.isFainted())
    events.emplace_back(FaintedEvent{ref});
  else
    itemHpCheck(state, data, ref, events);
}

// Item hooks in the residual window, faster side first (canon order:
// weather chip, then item heals, then status damage, then the orbs).
void applyItemHook(BattleState &state, const DataLoader &data, int side, EventLog &events,
                   void (Item::*hook)(ItemContext &) const) {
  BattlePokemon &p = state.active(side);
  if (p.isFainted())
    return;
  if (const Item *item = heldItem(p)) {
    CombatantRef ref{side, state.activeIndex[static_cast<size_t>(side)]};
    ItemContext ctx{state, data, events, ref};
    (item->*hook)(ctx);
  }
}

} // namespace

std::array<int, 2> BattleEngine::computeOrder(const BattleState &state, const Action &a0,
                                              const Action &a1, RNG &rng) const {
  // A charging Pokemon is locked: its priority comes from the charged move.
  auto priorityOf = [this, &state](int side, const Action &a) {
    const BattlePokemon &user = state.active(side);
    if (user.charging_move_id != kNoMove)
      return data_.moveByIndex(user.charging_move_id).priority;
    return actionPriority(a, data_, user);
  };
  int p0 = priorityOf(0, a0);
  int p1 = priorityOf(1, a1);
  if (p0 != p1)
    return (p0 > p1) ? std::array<int, 2>{0, 1} : std::array<int, 2>{1, 0};

  int first = fasterSide(state, rng);
  return {first, 1 - first};
}

void BattleEngine::executeAction(BattleState &state, int side, const Action &action, RNG &rng,
                                 EventLog &events) const {
  if (state.active(side).isFainted())
    return;

  BattlePokemon &user = state.active(side);
  CombatantRef userRef{side, state.activeIndex[static_cast<size_t>(side)]};

  // A charging Pokemon is locked into its two-turn move: the provided
  // action (even a switch) is ignored and the release happens now (ADR #29).
  bool releasing = user.charging_move_id != kNoMove;

  if (!releasing) {
    if (const auto *sw = std::get_if<SwitchAction>(&action)) {
      performSwitch(state, data_, side, sw->teamIndex, events);
      return;
    }
  }
  int moveId = kNoMove;
  int pivotTarget = -1;
  int ppSlot = -1; // slot to bill after the before-move gate; -1 = free
  const Move *movePtr = nullptr;
  if (releasing) {
    moveId = user.charging_move_id;
    user.charging_move_id = kNoMove;
    user.invulnerable_state = 0; // comes down even if the release misses
    movePtr = &data_.moveByIndex(moveId);
  } else {
    const auto &useMove = std::get<UseMove>(action);
    if (!canUseAnyMove(user)) {
      movePtr = &struggleMove(); // mandatory fallback, costs nothing (ADR #35)
    } else {
      ppSlot = useMove.moveIndex;
      moveId = user.move_ids[static_cast<size_t>(ppSlot)];
      movePtr = &data_.moveByIndex(moveId);
    }
    pivotTarget = useMove.pivotTarget;
  }
  const Move &move = *movePtr;

  if (!passesBeforeMove(user, userRef, rng, events))
    return; // an interrupted charge is lost (already cleared above)

  // PP burns when the move executes (ADR #35): a skipped turn doesn't pay,
  // a miss or failure does; a two-turn move pays on its charge turn only.
  if (ppSlot >= 0) {
    user.pp[static_cast<size_t>(ppSlot)] = std::max(0, user.pp[static_cast<size_t>(ppSlot)] - 1);
    // Choice items lock onto the first move actually used, hit or miss.
    if (const Item *item = heldItem(user)) {
      if (item->locksMove() && user.locked_move_id == kNoMove)
        user.locked_move_id = moveId;
    }
  }

  int otherSide = 1 - side;
  CombatantRef targetRef{otherSide, state.activeIndex[static_cast<size_t>(otherSide)]};

  // Charge turn of a two-turn move; SolarBeam skips it under the sun.
  if (!releasing && move.twoTurn != TwoTurn::None &&
      !(move.solarCharge && state.weather == Weather::Sun)) {
    events.emplace_back(ChargingEvent{userRef, move.name});
    user.charging_move_id = moveId;
    user.invulnerable_state = (move.twoTurn == TwoTurn::Fly)   ? 1
                              : (move.twoTurn == TwoTurn::Dig) ? 2
                                                               : 0;
    return;
  }

  events.emplace_back(MoveUsedEvent{userRef, move.name});

  const BattlePokemon &target = state.active(otherSide);

  // Semi-invulnerable target: automatic miss, unless Earthquake digs it out.
  bool digReached = target.invulnerable_state == 2 && move.hitsDig;
  if (target.invulnerable_state != 0 && !digReached && move.category != MoveCategory::Status) {
    events.emplace_back(MissedEvent{userRef, move.name});
    return;
  }

  // Protect blocks moves aimed at the target; self/field moves pass and
  // Whirlwind bypasses (ADR #30).
  if (target.protected_now != 0 && move.blockedByProtect && !move.bypassesProtect) {
    events.emplace_back(ProtectedEvent{targetRef});
    return;
  }

  // Accuracy: accuracy <= 0 never misses; stages use the (3+n)/3 table on
  // the combined stage (user Acc - target Eva), clamped (ADR #18 resolved).
  if (move.accuracy > 0) {
    int combined = user.stat_stages[static_cast<size_t>(StatIndex::Accuracy)] -
                   target.stat_stages[static_cast<size_t>(StatIndex::Evasion)];
    combined = std::clamp(combined, kMinStage, kMaxStage);
    int effAcc =
        static_cast<int>(static_cast<float>(move.accuracy) * accuracyStageMultiplier(combined));
    if (effAcc < 100 && !rng.chancePct(effAcc)) {
      events.emplace_back(MissedEvent{userRef, move.name});
      return;
    }
  }

  // Defender ability can void the move entirely (Levitate vs Ground).
  const Species &targetSp = data_.speciesByIndex(state.active(otherSide).species_id);
  if (const Ability *targetAbility = abilityByName(targetSp.ability)) {
    if (targetAbility->immuneToMove(move)) {
      events.emplace_back(AbilityTriggeredEvent{targetRef, targetAbility->name()});
      return;
    }
  }

  EffectContext ctx{state, data_, rng, events, userRef, targetRef, move, pivotTarget};
  for (const auto &effect : move.effects) {
    effect->apply(ctx);
    if (ctx.moveFailed)
      break; // immunity or failed set: the rest of the chain doesn't run
  }

  // Orbe Vie bills its 10% after a move that dealt damage. Slot-addressed
  // via userRef: correct even if a pivot already moved the user to the bench.
  if (ctx.lastDamageDealt > 0) {
    const BattlePokemon &mover =
        state.teams[static_cast<size_t>(userRef.side)][static_cast<size_t>(userRef.teamIndex)];
    if (const Item *item = heldItem(mover)) {
      ItemContext ictx{state, data_, events, userRef};
      item->onAfterDamagingMove(ictx);
    }
  }
}

void BattleEngine::checkAction(const BattleState &state, int side, const Action &action) const {
  std::string where = "side " + std::to_string(side) + ": ";

  if (state.active(side).isFainted())
    throw std::invalid_argument("resolveTurn: " + where +
                                "active is fainted, resolveReplacement required first");

  // Locked into a two-turn move: the action is ignored, accept anything.
  if (state.active(side).charging_move_id != kNoMove)
    return;

  if (const auto *sw = std::get_if<SwitchAction>(&action)) {
    if (!isValidSwitchTarget(state, side, sw->teamIndex))
      throw std::invalid_argument("resolveTurn: " + where + "invalid switch target " +
                                  std::to_string(sw->teamIndex));
    return;
  }

  const auto &useMove = std::get<UseMove>(action);
  const BattlePokemon &actor = state.active(side);
  if (!canUseAnyMove(actor))
    return; // out of PP (lock included): the engine substitutes Lutte (ADR #35)
  if (useMove.moveIndex < 0 || useMove.moveIndex >= kMaxMovesPerPokemon)
    throw std::invalid_argument("resolveTurn: " + where + "moveIndex out of range");
  if (state.active(side).move_ids[static_cast<size_t>(useMove.moveIndex)] == kNoMove)
    throw std::invalid_argument("resolveTurn: " + where + "empty move slot " +
                                std::to_string(useMove.moveIndex));
  if (state.active(side).pp[static_cast<size_t>(useMove.moveIndex)] <= 0)
    throw std::invalid_argument("resolveTurn: " + where + "no PP left in slot " +
                                std::to_string(useMove.moveIndex));
  if (choiceLockActive(actor) &&
      actor.move_ids[static_cast<size_t>(useMove.moveIndex)] != actor.locked_move_id)
    throw std::invalid_argument("resolveTurn: " + where + "choice-locked into another move");
  // pivotTarget is not checked here: it may become stale mid-turn (target
  // faints); PivotEffect re-validates and falls back to auto.
}

EventLog BattleEngine::startBattle(BattleState &state, RNG &rng) const {
  EventLog events;
  int first = fasterSide(state, rng);
  for (int side : {first, 1 - first}) {
    CombatantRef ref{side, state.activeIndex[static_cast<size_t>(side)]};
    const Species &sp = data_.speciesByIndex(state.active(side).species_id);
    if (const Ability *ability = abilityByName(sp.ability)) {
      AbilityContext ctx{state, data_, events, ref};
      ability->onSwitchIn(ctx);
    }
  }
  return events;
}

EventLog BattleEngine::resolveReplacement(BattleState &state, int side, int teamIndex) const {
  if (side < 0 || side >= kSideCount)
    throw std::invalid_argument("resolveReplacement: invalid side " + std::to_string(side));
  if (!state.active(side).isFainted())
    throw std::invalid_argument("resolveReplacement: side " + std::to_string(side) +
                                " active is not fainted");
  if (!isValidSwitchTarget(state, side, teamIndex))
    throw std::invalid_argument("resolveReplacement: invalid target " + std::to_string(teamIndex) +
                                " for side " + std::to_string(side));

  EventLog events;
  performSwitch(state, data_, side, teamIndex, events);
  return events;
}

EventLog BattleEngine::resolveTurn(BattleState &state, const Action &a0, const Action &a1,
                                   RNG &rng) const {
  checkAction(state, 0, a0);
  checkAction(state, 1, a1);

  EventLog events;
  state.turn += 1;

  auto order = computeOrder(state, a0, a1, rng);
  const Action *actions[2] = {&a0, &a1};

  for (int side : order) {
    if (state.isOver())
      break;
    executeAction(state, side, *actions[side], rng, events);
  }

  // Weather upkeep, before status residuals (canon order): the counter
  // ticks down; on reaching zero the weather ends with no chip that turn.
  if (!state.isOver() && state.weather != Weather::None) {
    state.weather_turns_left -= 1;
    if (state.weather_turns_left <= 0) {
      events.emplace_back(WeatherEndedEvent{state.weather});
      state.weather = Weather::None;
      state.weather_turns_left = 0;
    } else if (state.weather == Weather::Sand || state.weather == Weather::Hail) {
      int first = fasterSide(state, rng);
      applyWeatherChip(state, data_, first, events);
      applyWeatherChip(state, data_, 1 - first, events);
    }
  }

  // Item heals before the status ticks (Restes/Detritus, canon order).
  if (!state.isOver()) {
    int first = fasterSide(state, rng);
    applyItemHook(state, data_, first, events, &Item::onResidual);
    applyItemHook(state, data_, 1 - first, events, &Item::onResidual);
  }

  // End-of-turn residuals (burn/poison/toxic), faster side first.
  if (!state.isOver()) {
    int first = fasterSide(state, rng);
    applyResidual(state, data_, first, events);
    applyResidual(state, data_, 1 - first, events);
  }

  // Status orbs activate last: an OrbeFlamme burn only ticks next turn.
  if (!state.isOver()) {
    int first = fasterSide(state, rng);
    applyItemHook(state, data_, first, events, &Item::onTurnEnd);
    applyItemHook(state, data_, 1 - first, events, &Item::onTurnEnd);
  }

  // Volatile upkeep: flinch and Roost last one turn; a turn without a
  // successful Protect resets the chain (ADR #30).
  for (int side = 0; side < kSideCount; ++side) {
    BattlePokemon &p = state.active(side);
    p.flinched = 0;
    p.roosted = 0;
    if (p.protected_now == 0)
      p.protect_chain = 0;
    p.protected_now = 0;
  }
  return events;
}

} // namespace engine
