#pragma once

#include "engine/pokemon.hpp"
#include "engine/status.hpp"

#include <string>
#include <variant>
#include <vector>

namespace engine {

struct CombatantRef {
  int side;      // 0 or 1
  int teamIndex; // 0..kTeamSize-1
};

enum class SkipReason { Asleep, Frozen, FullyParalyzed };

struct MoveUsedEvent {
  CombatantRef user;
  std::string moveName;
};

struct DamageDealtEvent {
  CombatantRef target;
  int damage;
  float effectiveness; // 0.0, 0.5, 1.0, 2.0, 4.0
  bool wasStab;
};

struct FaintedEvent {
  CombatantRef who;
};

struct MissedEvent {
  CombatantRef user;
  std::string moveName;
};

struct StatusAppliedEvent {
  CombatantRef target;
  Status status;
};

// Immunity (type or chart), already statused, or Sleep Clause.
struct StatusFailedEvent {
  CombatantRef target;
  Status status;
};

struct StatusDamageEvent {
  CombatantRef target;
  Status status;
  int damage;
};

// Wake-up or thaw.
struct StatusCuredEvent {
  CombatantRef who;
  Status status;
};

struct MoveSkippedEvent {
  CombatantRef user;
  SkipReason reason;
};

// delta is the actual change applied after clamping (0 if already capped).
struct StatStageChangedEvent {
  CombatantRef target;
  StatIndex stat;
  int delta;
};

// Stat already at +6 (raise) or -6 (lower).
struct StatChangeFailedEvent {
  CombatantRef target;
  StatIndex stat;
  bool wasRaise;
};

using BattleEvent =
    std::variant<MoveUsedEvent, DamageDealtEvent, FaintedEvent, MissedEvent, StatusAppliedEvent,
                 StatusFailedEvent, StatusDamageEvent, StatusCuredEvent, MoveSkippedEvent,
                 StatStageChangedEvent, StatChangeFailedEvent>;
using EventLog = std::vector<BattleEvent>;

} // namespace engine
