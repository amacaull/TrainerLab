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
  int pivotTarget; // from UseMove; only read by PivotEffect
  // Set by an effect (immunity, failed set...) to stop the remaining chain:
  // Volt Switch vs a Ground type must not pivot, Rapid Spin blocked by a
  // Ghost must not clear hazards (canon).
  bool moveFailed = false;
  // Actual HP removed by the last DamageEffect (recoil basis, Dragon Tail).
  int lastDamageDealt = 0;
};

// To add a new effect: subclass Effect, register it in makeEffectFromJson
// (in data_loader.cpp).
class Effect {
public:
  virtual ~Effect() = default;
  virtual void apply(EffectContext &ctx) const = 0;
  virtual const char *name() const = 0;
};

using EffectPtr = std::unique_ptr<Effect>;

} // namespace engine
