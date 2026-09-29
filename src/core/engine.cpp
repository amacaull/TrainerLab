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
// The Choice lock ends when the item is consumed or knocked off (canon).
bool choiceLockActive(const BattlePokemon &p) {
  if (p.locked_move_id == kNoMove)
    return false;
  const Item *item = heldItem(p);
  return item != nullptr && item->locksMove();
}

// A locked Pokemon whose locked move is out of PP can only Struggle.
bool canUseAnyMove(const BattlePokemon &p) {
  if (!choiceLockActive(p))
    return p.hasUsablePp();
  for (int i = 0; i < kMaxMovesPerPokemon; ++i)
    if (p.move_ids[static_cast<size_t>(i)] == p.locked_move_id && p.pp[static_cast<size_t>(i)] > 0)
      return true;
  return false;
}

int actionPriority(const Action &a, const DataLoader &data, const BattlePokemon &user) {
  if (std::holds_alternative<SwitchAction>(a))
    return 6;
  if (!canUseAnyMove(user))
    return 0;
  const auto &useMove = std::get<UseMove>(a);
  int moveId = user.move_ids[static_cast<size_t>(useMove.moveIndex)];
  const Move &move = data.moveByIndex(moveId);
  int boost = 0;
  if (const Ability *ability = abilityByName(data.speciesByIndex(user.species_id).ability))
    boost = ability->priorityBoost(move);
  return move.priority + boost;
}

int effectiveSpeed(const BattleState &state, int side, const DataLoader &data) {
  const BattlePokemon &p = state.active(side);
  float mul = stageMultiplier(p.stat_stages[static_cast<size_t>(StatIndex::Spe)]);
  if (const Item *item = heldItem(p))
    mul *= item->statMultiplier(StatIndex::Spe);
  if (const Ability *ability = abilityOf(data, p))
    mul *= ability->speedMultiplier(state);
  int spd = static_cast<int>(static_cast<float>(p.stats.speed) * mul);
  if (p.status == Status::Paralysis)
    spd /= 2;
  return spd;
}

// Calls from BattleEngine members must stay qualified (::engine::fasterSide): the member of the
// same name would shadow this one.
int fasterSide(const BattleState &state, const DataLoader &data, RNG &rng) {
  int s0 = effectiveSpeed(state, 0, data);
  int s1 = effectiveSpeed(state, 1, data);
  if (s0 != s1)
    return (s0 > s1) ? 0 : 1;
  return rng.chance(0.5f) ? 0 : 1;
}

bool passesBeforeMove(BattlePokemon &user, const CombatantRef &ref, const Move &move, RNG &rng,
                      EventLog &events) {
  // Canon order: sleep and freeze first, then flinch, then paralysis. A
  // flinched sleeper still ticks its sleep counter.
  switch (user.status) {
  case Status::Sleep:
    if (user.status_turns > 0) {
      user.status_turns -= 1;
      if (!move.usableWhileAsleep) {
        events.emplace_back(MoveSkippedEvent{ref, SkipReason::Asleep});
        return false;
      }
      break; // SleepTalk acts while the counter keeps ticking
    }
    user.status = Status::None;
    user.sleep_self_inflicted = 0;
    events.emplace_back(StatusCuredEvent{ref, Status::Sleep});
    break;
  case Status::Freeze:
    if (move.thawsUser || rng.chance(kThawChance)) {
      user.status = Status::None;
      events.emplace_back(StatusCuredEvent{ref, Status::Freeze});
      break;
    }
    events.emplace_back(MoveSkippedEvent{ref, SkipReason::Frozen});
    return false;
  default:
    break;
  }
  if (user.flinched != 0) {
    user.flinched = 0;
    events.emplace_back(MoveSkippedEvent{ref, SkipReason::Flinched});
    return false;
  }
  if (user.status == Status::Paralysis && rng.chance(kFullParalysisChance)) {
    events.emplace_back(MoveSkippedEvent{ref, SkipReason::FullyParalyzed});
    return false;
  }
  return true;
}

void applyWeatherChip(BattleState &state, const DataLoader &data, int side, EventLog &events) {
  BattlePokemon &p = state.active(side);
  if (p.isFainted())
    return;

  const Species &sp = data.speciesByIndex(p.species_id);
  auto hasType = [&sp](Type t) { return sp.type1 == t || sp.type2 == t; };
  if (hasType(Type::Rock) || hasType(Type::Ground) || hasType(Type::Steel))
    return;

  int hpBefore = p.currentHp;
  int damage = std::max(1, p.stats.hp / kWeatherChipDenom);
  p.currentHp = std::max(0, p.currentHp - damage);
  CombatantRef ref{side, state.activeIndex[static_cast<size_t>(side)]};
  events.emplace_back(WeatherDamageEvent{ref, state.weather, damage});
  if (p.isFainted()) {
    events.emplace_back(FaintedEvent{ref});
  } else {
    itemHpCheck(state, data, ref, events);
    abilityHpCheck(state, data, ref, hpBefore, false, events);
  }
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
    p.status_turns += 1;
    damage = std::max(1, maxHp * p.status_turns / kToxicDamageDenom);
    break;
  default:
    return;
  }

  int hpBefore = p.currentHp;
  p.currentHp = std::max(0, p.currentHp - damage);
  CombatantRef ref{side, state.activeIndex[static_cast<size_t>(side)]};
  events.emplace_back(StatusDamageEvent{ref, p.status, damage});
  if (p.isFainted()) {
    events.emplace_back(FaintedEvent{ref});
  } else {
    itemHpCheck(state, data, ref, events);
    abilityHpCheck(state, data, ref, hpBefore, false, events);
  }
}

// Residual order (canon): weather, item heals, status damage, orbs.
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

  int first = ::engine::fasterSide(state, data_, rng);
  return {first, 1 - first};
}

void BattleEngine::executeAction(BattleState &state, int side, const Action &action,
                                 const Action *otherAction, bool targetAlreadyActed, RNG &rng,
                                 EventLog &events) const {
  if (state.active(side).isFainted())
    return;

  BattlePokemon &user = state.active(side);
  CombatantRef userRef{side, state.activeIndex[static_cast<size_t>(side)]};

  // A charging Pokemon is locked into its move: the chosen action, even a switch, is ignored.
  bool releasing = user.charging_move_id != kNoMove;

  if (!releasing) {
    if (const auto *sw = std::get_if<SwitchAction>(&action)) {
      performSwitch(state, data_, side, sw->teamIndex, events);
      return;
    }
  }
  int moveId = kNoMove;
  int pivotTarget = -1;
  int ppSlot = -1;
  const Move *movePtr = nullptr;
  if (releasing) {
    moveId = user.charging_move_id;
    user.charging_move_id = kNoMove;
    user.invulnerable_state = 0; // comes down even if the release misses
    movePtr = &data_.moveByIndex(moveId);
  } else {
    const auto &useMove = std::get<UseMove>(action);
    if (!canUseAnyMove(user)) {
      movePtr = &struggleMove();
    } else {
      ppSlot = useMove.moveIndex;
      moveId = user.move_ids[static_cast<size_t>(ppSlot)];
      movePtr = &data_.moveByIndex(moveId);
    }
    pivotTarget = useMove.pivotTarget;
  }
  const Move &move = *movePtr;

  if (!passesBeforeMove(user, userRef, move, rng, events))
    return;

  // PP is paid when the move executes: a skipped turn is free, a miss or a failure is not, and a
  // two-turn move pays on its charge turn.
  int ppSpent = 0;
  if (ppSlot >= 0) {
    int bill = 1;
    const BattlePokemon &foe = state.active(1 - side);
    if (!foe.isFainted() && (move.blockedByProtect || move.bypassesProtect)) {
      if (const Ability *foeAbility = abilityOf(data_, foe)) {
        if (foeAbility->pressuresPP())
          bill = 2;
      }
    }
    const int ppBefore = user.pp[static_cast<size_t>(ppSlot)];
    user.pp[static_cast<size_t>(ppSlot)] = std::max(0, ppBefore - bill);
    ppSpent = ppBefore - user.pp[static_cast<size_t>(ppSlot)];
    // Choice items lock onto the first move used, hit or miss.
    if (const Item *item = heldItem(user)) {
      if (item->locksMove() && user.locked_move_id == kNoMove)
        user.locked_move_id = moveId;
    }
  }

  int otherSide = 1 - side;

  if (!releasing && move.twoTurn != TwoTurn::None &&
      !(move.solarCharge && state.weather == Weather::Sun)) {
    events.emplace_back(ChargingEvent{userRef, move.name, ppSpent});
    user.charging_move_id = moveId;
    user.invulnerable_state = (move.twoTurn == TwoTurn::Fly)         ? 1
                              : (move.twoTurn == TwoTurn::Dig)       ? 2
                              : (move.twoTurn == TwoTurn::Disappear) ? 3
                                                                     : 0;
    return;
  }

  events.emplace_back(MoveUsedEvent{userRef, move.name, ppSpent});
  user.destiny_bond_active = 0; // until the user's next action

  const BattlePokemon &target = state.active(otherSide);

  // Usability checks come after the PP is paid (canon).
  if (move.firstTurnOnly && user.turns_on_field > 0) {
    events.emplace_back(MoveFailedEvent{userRef, move.name});
    return;
  }
  if (move.requiresTargetItem && heldItem(target) == nullptr) {
    events.emplace_back(MoveFailedEvent{userRef, move.name});
    return;
  }
  if (move.failsIfTargetNotAttacking) {
    bool targetAttacking = false;
    if (!targetAlreadyActed && otherAction != nullptr) {
      if (const auto *foeMove = std::get_if<UseMove>(otherAction)) {
        const BattlePokemon &foe = state.active(otherSide);
        if (!canUseAnyMove(foe)) {
          targetAttacking = true;
        } else {
          int foeMoveId = foe.move_ids[static_cast<size_t>(foeMove->moveIndex)];
          targetAttacking = data_.moveByIndex(foeMoveId).category != MoveCategory::Status;
        }
      }
    }
    if (!targetAttacking) {
      events.emplace_back(MoveFailedEvent{userRef, move.name});
      return;
    }
  }

  // Prankster only boosts the move the user picked, never a called one.
  bool pranksterBoosted = false;
  if (ppSlot >= 0) {
    if (const Ability *ability = abilityOf(data_, user))
      pranksterBoosted = ability->priorityBoost(move) > 0;
  }

  resolveHit(state, side, move, pivotTarget, targetAlreadyActed, pranksterBoosted, rng, events);

  // Recorded even on a miss: DestinyBond's no-repeat rule reads it.
  user.last_move_id = moveId;
}

void BattleEngine::resolveHit(BattleState &state, int side, const Move &move, int pivotTarget,
                              bool targetAlreadyActed, bool pranksterBoosted, RNG &rng,
                              EventLog &events) const {
  const int otherSide = 1 - side;
  CombatantRef userRef{side, state.activeIndex[static_cast<size_t>(side)]};
  CombatantRef targetRef{otherSide, state.activeIndex[static_cast<size_t>(otherSide)]};
  BattlePokemon &user = state.active(side);
  const BattlePokemon &target = state.active(otherSide);
  const Species &userSp = data_.speciesByIndex(user.species_id);

  // MagicBounce sends the move back at its user, skipping Protect and accuracy (canon).
  bool bounced = false;
  if (move.reflectable && !target.isFainted()) {
    if (const Ability *targetAbility = abilityOf(data_, target)) {
      if (targetAbility->bouncesStatusMoves()) {
        events.emplace_back(AbilityTriggeredEvent{targetRef, targetAbility->name()});
        std::swap(userRef, targetRef);
        bounced = true;
      }
    }
  }

  // Toxic thrown by a Poison-type never misses, semi-invulnerable target included (canon).
  const bool typeGuaranteesHit =
      move.alwaysHitsIfUserType != Type::Count &&
      (userSp.type1 == move.alwaysHitsIfUserType || userSp.type2 == move.alwaysHitsIfUserType);

  // A semi-invulnerable target dodges every move aimed at it, status moves included, unless the
  // move reaches it (Earthquake vs Dig).
  bool digReached = target.invulnerable_state == 2 && move.hitsDig;
  bool flyReached = target.invulnerable_state == 1 && move.hitsFly;
  if (!bounced && target.invulnerable_state != 0 && !digReached && !flyReached &&
      move.blockedByProtect && !typeGuaranteesHit) {
    events.emplace_back(MissedEvent{userRef, move.name});
    return;
  }

  if (!bounced && target.protected_now != 0 && move.blockedByProtect && !move.bypassesProtect) {
    events.emplace_back(ProtectedEvent{targetRef});
    if (move.makesContact && target.protect_contact_status != 0) {
      Status status = static_cast<Status>(target.protect_contact_status);
      if (user.status == Status::None && !typeImmuneToStatus(status, userSp)) {
        user.status = status;
        events.emplace_back(StatusAppliedEvent{userRef, status});
      }
    }
    return;
  }

  // Prankster (gen 7+): a boosted status move aimed at a Dark-type fails.
  if (!bounced && pranksterBoosted && move.category == MoveCategory::Status &&
      move.blockedByProtect && !target.isFainted()) {
    const Species &targetSp = data_.speciesByIndex(target.species_id);
    if (targetSp.type1 == Type::Dark || targetSp.type2 == Type::Dark) {
      events.emplace_back(MoveFailedEvent{userRef, move.name});
      return;
    }
  }

  // Unaware ignores the opposing accuracy or evasion stage (canon).
  int accuracy = move.accuracy;
  for (const auto &[w, acc] : move.accuracyInWeather)
    if (w == state.weather)
      accuracy = acc;
  if (typeGuaranteesHit)
    accuracy = 0;
  if (!bounced && accuracy > 0) {
    int accStage = user.stat_stages[static_cast<size_t>(StatIndex::Accuracy)];
    int evaStage = target.stat_stages[static_cast<size_t>(StatIndex::Evasion)];
    const Ability *userAbility = abilityOf(data_, user);
    const Ability *targetAbility = abilityOf(data_, target);
    if (targetAbility && targetAbility->ignoresStages())
      accStage = 0;
    if (userAbility && userAbility->ignoresStages())
      evaStage = 0;
    int combined = std::clamp(accStage - evaStage, kMinStage, kMaxStage);
    int effAcc = static_cast<int>(static_cast<float>(accuracy) * accuracyStageMultiplier(combined));
    if (effAcc < 100 && !rng.chancePct(effAcc)) {
      events.emplace_back(MissedEvent{userRef, move.name});
      return;
    }
  }

  {
    const BattlePokemon &effTarget =
        state.teams[static_cast<size_t>(targetRef.side)][static_cast<size_t>(targetRef.teamIndex)];
    if (const Ability *targetAbility = abilityOf(data_, effTarget)) {
      if (targetAbility->immuneToMove(move)) {
        events.emplace_back(AbilityTriggeredEvent{targetRef, targetAbility->name()});
        AbilityContext actx{state, data_, events, targetRef};
        targetAbility->onMoveAbsorbed(actx);
        return;
      }
    }
  }

  EffectContext ctx{state, data_, rng, events, userRef, targetRef, move, pivotTarget};
  ctx.targetAlreadyActed = targetAlreadyActed;
  for (const auto &effect : move.effects) {
    effect->apply(ctx);
    if (ctx.moveFailed)
      break;
    if (!stillOnField(state, targetRef.side, targetRef.teamIndex))
      break; // EmergencyExit took the target out
  }

  // Addressed by slot: the user may already have pivoted out.
  if (ctx.lastDamageDealt > 0) {
    const BattlePokemon &mover =
        state.teams[static_cast<size_t>(userRef.side)][static_cast<size_t>(userRef.teamIndex)];
    if (const Item *item = heldItem(mover)) {
      ItemContext ictx{state, data_, events, userRef};
      item->onAfterDamagingMove(ictx);
    }
    const BattlePokemon &struck =
        state.teams[static_cast<size_t>(targetRef.side)][static_cast<size_t>(targetRef.teamIndex)];
    if (const Ability *moverAbility = abilityOf(data_, mover)) {
      AbilityContext actx{state, data_, events, userRef};
      // Magician must not lift an item off a Pokemon that left the field.
      if (!struck.isFainted() && stillOnField(state, targetRef.side, targetRef.teamIndex))
        moverAbility->onAfterDamagingMove(actx, targetRef);
      if (struck.isFainted() && !mover.isFainted())
        moverAbility->onAfterKO(actx);
    }
  }

  // The move called by SleepTalk goes through the same pipeline (canon).
  if (ctx.calledMoveId >= 0 && !state.active(side).isFainted() && !state.isOver()) {
    resolveHit(state, side, data_.moveByIndex(ctx.calledMoveId), -1, targetAlreadyActed, false,
               rng, events);
  }
}

namespace {
// The subcode is the only part of a refusal Rust reads (README.md, section 6).
[[noreturn]] void refuse(const char *code, const std::string &detail) {
  throw std::invalid_argument(std::string(code) + ": " + detail);
}
} // namespace

void BattleEngine::checkAction(const BattleState &state, int side, const Action &action) const {
  std::string where = "side " + std::to_string(side) + ": ";

  if (state.active(side).isFainted())
    refuse("FAINTED",
           "resolveTurn: " + where + "active is fainted, resolveReplacement required first");

  if (state.active(side).charging_move_id != kNoMove)
    return;

  if (const auto *sw = std::get_if<SwitchAction>(&action)) {
    if (!isValidSwitchTarget(state, side, sw->teamIndex))
      refuse("INVALID_SWITCH",
             "resolveTurn: " + where + "invalid switch target " + std::to_string(sw->teamIndex));
    return;
  }

  const auto &useMove = std::get<UseMove>(action);
  const BattlePokemon &actor = state.active(side);
  if (!canUseAnyMove(actor))
    return;
  if (useMove.moveIndex < 0 || useMove.moveIndex >= kMaxMovesPerPokemon)
    refuse("BAD_SLOT", "resolveTurn: " + where + "moveIndex out of range");
  if (state.active(side).move_ids[static_cast<size_t>(useMove.moveIndex)] == kNoMove)
    refuse("EMPTY_SLOT",
           "resolveTurn: " + where + "empty move slot " + std::to_string(useMove.moveIndex));
  if (state.active(side).pp[static_cast<size_t>(useMove.moveIndex)] <= 0)
    refuse("NO_PP",
           "resolveTurn: " + where + "no PP left in slot " + std::to_string(useMove.moveIndex));
  if (choiceLockActive(actor) &&
      actor.move_ids[static_cast<size_t>(useMove.moveIndex)] != actor.locked_move_id)
    refuse("CHOICE_LOCKED", "resolveTurn: " + where + "choice-locked into another move");
  // pivotTarget may go stale mid-turn: PivotEffect re-checks it.
}

EventLog BattleEngine::startBattle(BattleState &state, RNG &rng) const {
  EventLog events;
  int first = ::engine::fasterSide(state, data_, rng);
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

int BattleEngine::fasterSide(const BattleState &state, RNG &rng) const {
  return ::engine::fasterSide(state, data_, rng);
}

EventLog BattleEngine::resolveReplacement(BattleState &state, int side, int teamIndex) const {
  if (side < 0 || side >= kSideCount)
    refuse("BAD_SIDE", "resolveReplacement: invalid side " + std::to_string(side));
  if (!state.active(side).isFainted())
    refuse("NOT_FAINTED",
           "resolveReplacement: side " + std::to_string(side) + " active is not fainted");
  if (!isValidSwitchTarget(state, side, teamIndex))
    refuse("INVALID_SWITCH", "resolveReplacement: invalid target " + std::to_string(teamIndex) +
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

  bool firstActed = false;
  for (int side : order) {
    if (state.isOver())
      break;
    executeAction(state, side, *actions[side], actions[1 - side], firstActed, rng, events);
    firstActed = true;
  }

  // On its last turn the weather ends without dealing chip damage (canon).
  if (!state.isOver() && state.weather != Weather::None &&
      state.weather != Weather::StrongWinds) {
    state.weather_turns_left -= 1;
    if (state.weather_turns_left <= 0) {
      events.emplace_back(WeatherEndedEvent{state.weather});
      state.weather = Weather::None;
      state.weather_turns_left = 0;
    } else if (state.weather == Weather::Sand) {
      int first = ::engine::fasterSide(state, data_, rng);
      applyWeatherChip(state, data_, first, events);
      applyWeatherChip(state, data_, 1 - first, events);
    }
  }

  if (!state.isOver() && state.terrain != Terrain::None) {
    state.terrain_turns_left -= 1;
    if (state.terrain_turns_left <= 0) {
      events.emplace_back(TerrainEndedEvent{state.terrain});
      state.terrain = Terrain::None;
      state.terrain_turns_left = 0;
    }
  }

  // Wish lands at the end of the NEXT turn, on whoever holds the slot.
  if (!state.isOver()) {
    for (int side = 0; side < kSideCount; ++side) {
      int &wt = state.wish_turns[static_cast<size_t>(side)];
      if (wt <= 0)
        continue;
      wt -= 1;
      if (wt > 0)
        continue;
      BattlePokemon &p = state.active(side);
      int amount = state.wish_heal[static_cast<size_t>(side)];
      state.wish_heal[static_cast<size_t>(side)] = 0;
      if (p.isFainted() || p.currentHp >= p.stats.hp)
        continue;
      int healed = std::min(amount, p.stats.hp - p.currentHp);
      p.currentHp += healed;
      CombatantRef wref{side, state.activeIndex[static_cast<size_t>(side)]};
      events.emplace_back(HealedEvent{wref, healed});
    }
  }

  if (!state.isOver()) {
    int first = ::engine::fasterSide(state, data_, rng);
    applyItemHook(state, data_, first, events, &Item::onResidual);
    applyItemHook(state, data_, 1 - first, events, &Item::onResidual);
  }

  if (!state.isOver()) {
    int first = ::engine::fasterSide(state, data_, rng);
    applyResidual(state, data_, first, events);
    applyResidual(state, data_, 1 - first, events);
  }

  // Orbs last: a Flame Orb burn only deals damage from the next turn (canon).
  if (!state.isOver()) {
    int first = ::engine::fasterSide(state, data_, rng);
    applyItemHook(state, data_, first, events, &Item::onTurnEnd);
    applyItemHook(state, data_, 1 - first, events, &Item::onTurnEnd);
  }

  for (int side = 0; side < kSideCount; ++side) {
    BattlePokemon &p = state.active(side);
    if (!p.isFainted())
      p.turns_on_field += 1;
  }

  for (int side = 0; side < kSideCount; ++side) {
    int &veil = state.aurora_veil_turns[static_cast<size_t>(side)];
    if (veil > 0) {
      veil -= 1;
      if (veil == 0)
        events.emplace_back(ScreenEndedEvent{side});
    }
  }

  // A turn without a successful Protect resets the chain.
  for (int side = 0; side < kSideCount; ++side) {
    BattlePokemon &p = state.active(side);
    p.flinched = 0;
    p.roosted = 0;
    if (p.protected_now == 0)
      p.protect_chain = 0;
    p.protected_now = 0;
    p.protect_contact_status = 0;
  }
  return events;
}
} // namespace engine
