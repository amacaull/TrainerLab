#pragma once

#include "engine/core/events.hpp"

#include <memory>

namespace engine {
struct BattleState;
struct Move;
class RNG;
class DataLoader;

struct EffectContext {
  BattleState &state;
  const DataLoader &data;
  RNG &rng;
  EventLog &events;
  CombatantRef user;
  CombatantRef target;
  const Move &move;
  int pivotTarget;
  // Stops the rest of the chain: Volt Switch into a Ground-type must not pivot (canon).
  bool moveFailed = false;
  int lastDamageDealt = 0;
  int powerOverride = 0;
  int multiHitIndex = -1;
  bool targetAlreadyActed = false;
  int calledMoveId = -1;
};

class Effect {
public:
  virtual ~Effect() = default;
  virtual void apply(EffectContext &ctx) const = 0;
  virtual const char *name() const = 0;
};

using EffectPtr = std::unique_ptr<Effect>;
} // namespace engine
