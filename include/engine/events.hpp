#pragma once

#include <string>
#include <variant>
#include <vector>

namespace engine {

// Identifies a Pokemon in battle by (side, index). Stays serializable.
struct CombatantRef {
    int side;       // 0 or 1
    int teamIndex;  // 0..teamSize-1
};

// Events emitted by the engine during a turn. Used for logging, replay,
// frontend animations, and debugging.

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
