#pragma once

#include "engine/events.hpp"

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
