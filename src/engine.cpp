#include "engine/engine.hpp"

#include "engine/effect.hpp"
#include "engine/move.hpp"
#include "engine/pokemon.hpp"
#include "engine/status.hpp"

#include <algorithm>
#include <variant>

namespace engine {

namespace {

// Switch actions take priority over moves (+6 brackets above any move).
int actionPriority(const Action &a, const DataLoader &data, const BattlePokemon &user) {
  if (std::holds_alternative<SwitchAction>(a))
    return 6;
  const auto &useMove = std::get<UseMove>(a);
  int moveId = user.move_ids[static_cast<size_t>(useMove.moveIndex)];
  return data.moveByIndex(moveId).priority;
}

int effectiveSpeed(const BattlePokemon &p) {
  int spd = p.stats.speed;
  if (p.status == Status::Paralysis)
    spd /= 2;
  return spd;
}

// Sleep/freeze/paralysis gate. Returns true if the user can act this turn;
// handles the wake-up and thaw transitions (mutates status counters).
bool passesBeforeMove(BattlePokemon &user, const CombatantRef &ref, RNG &rng, EventLog &events) {
  switch (user.status) {
  case Status::Sleep:
    if (user.status_turns > 0) {
      user.status_turns -= 1;
      events.emplace_back(MoveSkippedEvent{ref, SkipReason::Asleep});
      return false;
    }
    user.status = Status::None;
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

void applyResidual(BattleState &state, int side, EventLog &events) {
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
}

} // namespace

std::array<int, 2> BattleEngine::computeOrder(const BattleState &state, const Action &a0,
                                              const Action &a1) const {
  int p0 = actionPriority(a0, data_, state.active(0));
  int p1 = actionPriority(a1, data_, state.active(1));
  if (p0 != p1)
    return (p0 > p1) ? std::array<int, 2>{0, 1} : std::array<int, 2>{1, 0};

  int s0 = effectiveSpeed(state.active(0));
  int s1 = effectiveSpeed(state.active(1));
  if (s0 != s1)
    return (s0 > s1) ? std::array<int, 2>{0, 1} : std::array<int, 2>{1, 0};

  // TODO phase 1 followup: random speed-tie break.
  return {0, 1};
}

void BattleEngine::executeAction(BattleState &state, int side, const Action &action, RNG &rng,
                                 EventLog &events) const {
  if (state.active(side).isFainted())
    return;

  // TODO phase 4: implement switch (+ reset Toxic counter on switch-out).
  if (std::holds_alternative<SwitchAction>(action))
    return;

  BattlePokemon &user = state.active(side);
  CombatantRef userRef{side, state.activeIndex[static_cast<size_t>(side)]};

  if (!passesBeforeMove(user, userRef, rng, events))
    return;

  const auto &useMove = std::get<UseMove>(action);
  int moveId = user.move_ids[static_cast<size_t>(useMove.moveIndex)];
  const Move &move = data_.moveByIndex(moveId);

  int otherSide = 1 - side;
  CombatantRef targetRef{otherSide, state.activeIndex[static_cast<size_t>(otherSide)]};

  events.emplace_back(MoveUsedEvent{userRef, move.name});

  if (move.accuracy < 100 && !rng.chancePct(move.accuracy)) {
    events.emplace_back(MissedEvent{userRef, move.name});
    return;
  }

  EffectContext ctx{state, data_, rng, events, userRef, targetRef, move};
  for (const auto &effect : move.effects) {
    effect->apply(ctx);
  }
}

EventLog BattleEngine::resolveTurn(BattleState &state, const Action &a0, const Action &a1,
                                   RNG &rng) const {
  EventLog events;
  state.turn += 1;

  auto order = computeOrder(state, a0, a1);
  const Action *actions[2] = {&a0, &a1};

  for (int side : order) {
    if (state.isOver())
      break;
    executeAction(state, side, *actions[side], rng, events);
  }

  // End-of-turn residuals (burn/poison/toxic), faster side first.
  if (!state.isOver()) {
    int first = effectiveSpeed(state.active(0)) >= effectiveSpeed(state.active(1)) ? 0 : 1;
    applyResidual(state, first, events);
    applyResidual(state, 1 - first, events);
  }
  return events;
}

} // namespace engine
