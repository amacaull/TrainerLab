#pragma once

#include "engine/core/battle_state.hpp"
#include "engine/core/data_loader.hpp"
#include "engine/core/events.hpp"
#include "engine/core/rng.hpp"

namespace engine {

class BattleEngine {
public:
  explicit BattleEngine(const DataLoader &data) : data_(data) {}

  // Fires both leads' on_switch_in abilities (faster side first). Call once
  // before the first resolveTurn.
  EventLog startBattle(BattleState &state, RNG &rng) const;

  EventLog resolveTurn(BattleState &state, const Action &actionP0, const Action &actionP1,
                       RNG &rng) const;

  // Showdown-style replacement phase (ADR #19): after a turn leaves an
  // active fainted, the caller must send the chosen replacement here before
  // the next resolveTurn. Free switch: no turn increment, no residuals.
  // Throws std::invalid_argument if the active is not fainted or the target
  // is invalid.
  EventLog resolveReplacement(BattleState &state, int side, int teamIndex) const;

private:
  // Throws std::invalid_argument on any illegal action, before any mutation.
  void checkAction(const BattleState &state, int side, const Action &action) const;

  // Returns {first_side, second_side} ordered by priority, then speed.
  std::array<int, 2> computeOrder(const BattleState &state, const Action &a0, const Action &a1,
                                  RNG &rng) const;

  void executeAction(BattleState &state, int side, const Action &action, RNG &rng,
                     EventLog &events) const;

  const DataLoader &data_;
};

} // namespace engine
