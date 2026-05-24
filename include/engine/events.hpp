#pragma once

#include <string>
#include <variant>
#include <vector>

namespace engine {

struct CombatantRef {
  int side;      // 0 or 1
  int teamIndex; // 0..kTeamSize-1
};

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

using BattleEvent = std::variant<MoveUsedEvent, DamageDealtEvent, FaintedEvent, MissedEvent>;
using EventLog = std::vector<BattleEvent>;

} // namespace engine
