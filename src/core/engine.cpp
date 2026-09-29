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

// Struggle fallback test (ADR #35), lock-aware: a locked Pokemon whose locked
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
    return 0; // will be substituted with Struggle (ADR #35)
  const auto &useMove = std::get<UseMove>(a);
  int moveId = user.move_ids[static_cast<size_t>(useMove.moveIndex)];
  const Move &move = data.moveByIndex(moveId);
  int boost = 0;
  if (const Ability *ability = abilityByName(data.speciesByIndex(user.species_id).ability))
    boost = ability->priorityBoost(move); // Prankster: +1 on Status moves
  return move.priority + boost;
}

int effectiveSpeed(const BattleState &state, int side, const DataLoader &data) {
  const BattlePokemon &p = state.active(side);
  float mul = stageMultiplier(p.stat_stages[static_cast<size_t>(StatIndex::Spe)]);
  if (const Item *item = heldItem(p)) // ChoiceScarf x1.5: changes turn order
    mul *= item->statMultiplier(StatIndex::Spe);
  if (const Ability *ability = abilityOf(data, p))
    mul *= ability->speedMultiplier(state); // SwiftSwim & co: x2 under their weather
  int spd = static_cast<int>(static_cast<float>(p.stats.speed) * mul);
  if (p.status == Status::Paralysis)
    spd /= 2;
  return spd;
}

// Speed ties are broken randomly (ADR #31).
//
// BattleEngine has a public fasterSide/2 member forwarding here (D6). Class
// scope beats namespace scope, so every call from inside a member must stay
// explicitly qualified: dropping the :: silently resolves to the member and
// fails to compile on the argument count.
int fasterSide(const BattleState &state, const DataLoader &data, RNG &rng) {
  int s0 = effectiveSpeed(state, 0, data);
  int s1 = effectiveSpeed(state, 1, data);
  if (s0 != s1)
    return (s0 > s1) ? 0 : 1;
  return rng.chance(0.5f) ? 0 : 1;
}

// Sleep/freeze/paralysis gate. Returns true if the user can act this turn;
// handles the wake-up and thaw transitions (mutates status counters).
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
      break; // SleepTalk: acts while the counter keeps ticking
    }
    user.status = Status::None;
    user.sleep_self_inflicted = 0;
    events.emplace_back(StatusCuredEvent{ref, Status::Sleep});
    break;
  case Status::Freeze:
    if (move.thawsUser || rng.chance(kThawChance)) { // Scald / FlareBlitz melt their user
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

// Only sand chips (Rock/Ground/Steel immune); snow does not chip (ADR #37).
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
    p.status_turns += 1; // ramps: n/16 on the n-th turn under Toxic
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

  if (!passesBeforeMove(user, userRef, move, rng, events))
    return; // an interrupted charge is lost (already cleared above)

  // PP burns when the move executes (ADR #35): a skipped turn doesn't pay,
  // a miss or failure does; a two-turn move pays on its charge turn only.
  int ppSpent = 0;
  if (ppSlot >= 0) {
    int bill = 1;
    // Pressure: targeting its holder costs one extra PP.
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
    // Choice items lock onto the first move actually used, hit or miss.
    if (const Item *item = heldItem(user)) {
      if (item->locksMove() && user.locked_move_id == kNoMove)
        user.locked_move_id = moveId;
    }
  }

  int otherSide = 1 - side;

  // Charge turn of a two-turn move; SolarBeam skips it under the sun.
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
  user.destiny_bond_active = 0; // the bond holds until the next action only

  const BattlePokemon &target = state.active(otherSide);

  // Usability gates — the PP is already paid (canon).
  if (move.firstTurnOnly && user.turns_on_field > 0) {
    events.emplace_back(MoveFailedEvent{userRef, move.name});
    return;
  }
  if (move.requiresTargetItem && heldItem(target) == nullptr) {
    events.emplace_back(MoveFailedEvent{userRef, move.name});
    return;
  }
  if (move.failsIfTargetNotAttacking) {
    // SuckerPunch: the target must be about to throw a damaging move.
    bool targetAttacking = false;
    if (!targetAlreadyActed && otherAction != nullptr) {
      if (const auto *foeMove = std::get_if<UseMove>(otherAction)) {
        const BattlePokemon &foe = state.active(otherSide);
        if (!canUseAnyMove(foe)) {
          targetAttacking = true; // Struggle is very much an attack
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

  // Recorded even on a miss or a block: DestinyBond's no-repeat rule reads it.
  user.last_move_id = moveId; // Struggle leaves kNoMove: it never chains anything
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

  // MagicBounce: a reflectable move re-runs with the roles swapped and
  // skips Protect and accuracy (canon). A bounced Stealth Rock lands on
  // the original setter's side.
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

  // Toxic thrown by a Poison-type never misses, semi-invulnerable target included.
  const bool typeGuaranteesHit =
      move.alwaysHitsIfUserType != Type::Count &&
      (userSp.type1 == move.alwaysHitsIfUserType || userSp.type2 == move.alwaysHitsIfUserType);

  // Semi-invulnerable target: every move aimed at it misses, status moves
  // included, unless Earthquake digs it out. Self and field moves still work.
  bool digReached = target.invulnerable_state == 2 && move.hitsDig;
  bool flyReached = target.invulnerable_state == 1 && move.hitsFly;
  if (!bounced && target.invulnerable_state != 0 && !digReached && !flyReached &&
      move.blockedByProtect && !typeGuaranteesHit) {
    events.emplace_back(MissedEvent{userRef, move.name});
    return;
  }

  // Protect blocks moves aimed at the target; self/field moves pass and
  // Whirlwind bypasses (ADR #30).
  if (!bounced && target.protected_now != 0 && move.blockedByProtect && !move.bypassesProtect) {
    events.emplace_back(ProtectedEvent{targetRef});
    // BanefulBunker: a contact attacker walks into the status.
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

  // Accuracy: accuracy <= 0 never misses; stages use the (3+n)/3 table on
  // the combined stage (user Acc - target Eva), clamped (ADR #18 resolved).
  // Unaware ignores the other side's accuracy or evasion stage (canon).
  int accuracy = move.accuracy;
  for (const auto &[w, acc] : move.accuracyInWeather)
    if (w == state.weather)
      accuracy = acc; // 0 = never miss under this weather (Blizzard in snow)
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

  // Defender ability can void the move entirely (Levitate vs Ground),
  // with its absorption side effect (VoltAbsorb, LightningRod, FlashFire).
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
      break; // immunity or failed set: the rest of the chain doesn't run
    if (!stillOnField(state, targetRef.side, targetRef.teamIndex))
      break; // EmergencyExit pulled the target out: no status, no stat drop,
             // no phazing aimed at a Pokemon that is no longer there
  }

  // Post-hit window, slot-addressed via userRef (correct even if a pivot
  // already moved the user to the bench): Life Orb recoil, Magician theft,
  // Moxie on a KO.
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

  // SleepTalk: the called move goes through this same pipeline, so Protect,
  // immunities, invulnerability and accuracy all apply to it (canon).
  if (ctx.calledMoveId >= 0 && !state.active(side).isFainted() && !state.isOver()) {
    resolveHit(state, side, data_.moveByIndex(ctx.calledMoveId), -1, targetAlreadyActed, false,
               rng, events);
  }
}

namespace {

// README.md section 6 (errors): the subcode is the only machine-readable part of
// a refusal. cxx transports what() and loses the exception type, and an
// English sentence is not something a client can translate or act on. The
// sentence stays behind it, for the server log.
[[noreturn]] void refuse(const char *code, const std::string &detail) {
  throw std::invalid_argument(std::string(code) + ": " + detail);
}

} // namespace

void BattleEngine::checkAction(const BattleState &state, int side, const Action &action) const {
  std::string where = "side " + std::to_string(side) + ": ";

  if (state.active(side).isFainted())
    refuse("FAINTED",
           "resolveTurn: " + where + "active is fainted, resolveReplacement required first");

  // Locked into a two-turn move: the action is ignored, accept anything.
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
    return; // out of PP (lock included): the engine substitutes Struggle (ADR #35)
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
  // pivotTarget is not checked here: it may become stale mid-turn (target
  // faints); PivotEffect re-validates and falls back to auto.
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

  // Weather upkeep, before status residuals (canon order): the counter
  // ticks down; on reaching zero the weather ends with no chip that turn.
  if (!state.isOver() && state.weather != Weather::None &&
      state.weather != Weather::StrongWinds) { // presence-bound: no countdown (ADR #47)
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

  // Terrain ticks like the weather, right after it (ADR #38).
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

  // Item heals before the status ticks (Leftovers/BlackSludge, canon order).
  if (!state.isOver()) {
    int first = ::engine::fasterSide(state, data_, rng);
    applyItemHook(state, data_, first, events, &Item::onResidual);
    applyItemHook(state, data_, 1 - first, events, &Item::onResidual);
  }

  // End-of-turn residuals (burn/poison/toxic), faster side first.
  if (!state.isOver()) {
    int first = ::engine::fasterSide(state, data_, rng);
    applyResidual(state, data_, first, events);
    applyResidual(state, data_, 1 - first, events);
  }

  // Status orbs activate last: an FlameOrb burn only ticks next turn.
  if (!state.isOver()) {
    int first = ::engine::fasterSide(state, data_, rng);
    applyItemHook(state, data_, first, events, &Item::onTurnEnd);
    applyItemHook(state, data_, 1 - first, events, &Item::onTurnEnd);
  }

  for (int side = 0; side < kSideCount; ++side) {
    BattlePokemon &p = state.active(side);
    if (!p.isFainted())
      p.turns_on_field += 1; // closes the FakeOut / FirstImpression window
  }

  // Screens count down last (ADR #39).
  for (int side = 0; side < kSideCount; ++side) {
    int &veil = state.aurora_veil_turns[static_cast<size_t>(side)];
    if (veil > 0) {
      veil -= 1;
      if (veil == 0)
        events.emplace_back(ScreenEndedEvent{side});
    }
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
    p.protect_contact_status = 0;
  }
  return events;
}

} // namespace engine
