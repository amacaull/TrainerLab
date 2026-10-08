#pragma once

#include "engine/core/battle_state.hpp"
#include "engine/core/data_loader.hpp"
#include "engine/core/events.hpp"
#include "engine/core/rng.hpp"

namespace engine {
class BattleEngine {
public:
  explicit BattleEngine(const DataLoader &data) : data_(data) {}

  // Fires both leads' entry abilities. Call once, before the first resolveTurn.
  EventLog startBattle(BattleState &state, RNG &rng) const;

  EventLog resolveTurn(BattleState &state, const Action &actionP0, const Action &actionP1,
                       RNG &rng) const;

  // After a turn leaves an active fainted, the caller must send the replacement here before the
  // next resolveTurn. A free switch: no turn, no residuals.
  EventLog resolveReplacement(BattleState &state, int side, int teamIndex) const;

  // Exposed so the caller orders simultaneous replacements: entry abilities land differently
  // depending on who arrives first.
  int fasterSide(const BattleState &state, RNG &rng) const;

private:
  // Throws on any illegal action, before any mutation.
  void checkAction(const BattleState &state, int side, const Action &action) const;

  std::array<int, 2> computeOrder(const BattleState &state, const Action &a0, const Action &a1,
                                  RNG &rng) const;

  void executeAction(BattleState &state, int side, const Action &action, const Action *otherAction,
                     bool targetAlreadyActed, RNG &rng, EventLog &events) const;

  void resolveHit(BattleState &state, int side, const Move &move, int pivotTarget,
                  bool targetAlreadyActed, bool pranksterBoosted, RNG &rng,
                  EventLog &events) const;

  const DataLoader &data_;
};
} // namespace engine
