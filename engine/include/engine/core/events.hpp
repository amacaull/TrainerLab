#pragma once

#include "engine/model/field.hpp"
#include "engine/model/pokemon.hpp"
#include "engine/model/status.hpp"

#include <string>
#include <variant>
#include <vector>

namespace engine {
struct CombatantRef {
  int side;
  int teamIndex;
};

// Crosses the FFI as int: do not reorder.
enum class SkipReason : int { Asleep = 0, Frozen, FullyParalyzed, Flinched };

struct MoveUsedEvent {
  CombatantRef user;
  std::string moveName;
  int ppSpent = 0; // PP actually paid: 2 against Pressure, 0 for a release, Struggle or a called move
};

struct DamageDealtEvent {
  CombatantRef target;
  int damage;
  float effectiveness;
  bool wasStab;
  bool wasCrit = false;
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

struct StatusFailedEvent {
  CombatantRef target;
  Status status;
};

struct StatusDamageEvent {
  CombatantRef target;
  Status status;
  int damage;
};

struct StatusCuredEvent {
  CombatantRef who;
  Status status;
};

struct MoveSkippedEvent {
  CombatantRef user;
  SkipReason reason;
};

struct StatStageChangedEvent {
  CombatantRef target;
  StatIndex stat;
  int delta;
};

struct StatChangeFailedEvent {
  CombatantRef target;
  StatIndex stat;
  bool wasRaise;
};

struct SwitchedOutEvent {
  CombatantRef who;
};

struct SwitchedInEvent {
  CombatantRef who;
};

struct AbilityTriggeredEvent {
  CombatantRef who;
  std::string ability;
};

struct MoveFailedEvent {
  CombatantRef user;
  std::string moveName;
};

struct WeatherStartedEvent {
  Weather weather;
};

struct WeatherEndedEvent {
  Weather weather;
};

struct WeatherDamageEvent {
  CombatantRef target;
  Weather weather;
  int damage;
};

struct HazardSetEvent {
  int side;
  HazardKind hazard;
  int layers;
};

struct HazardDamageEvent {
  CombatantRef target;
  HazardKind hazard;
  int damage;
};

struct HazardsClearedEvent {
  int side;
};

struct ToxicSpikesAbsorbedEvent {
  CombatantRef who;
};

struct HealedEvent {
  CombatantRef who;
  int amount;
};

struct RecoilDamageEvent {
  CombatantRef who;
  int damage;
};

struct TerrainStartedEvent {
  Terrain terrain;
};

struct TerrainEndedEvent {
  Terrain terrain;
};

struct ScreenStartedEvent {
  int side;
  int turns;
};

struct ScreenEndedEvent {
  int side;
};

struct DestinyBondTriggeredEvent {
  CombatantRef dragged;
};

struct ItemTriggeredEvent {
  CombatantRef who;
  std::string itemName;
};

struct ItemConsumedEvent {
  CombatantRef who;
  std::string itemName;
};

struct ItemDamageEvent {
  CombatantRef who;
  std::string itemName;
  int damage;
};

// Kept apart from ItemDamageEvent: at the FFI boundary the name resolves against the ability
// catalog, not the item one.
struct AbilityDamageEvent {
  CombatantRef who;
  std::string ability;
  int damage;
};

struct ItemKnockedOffEvent {
  CombatantRef who;
  std::string itemName;
};

struct ChargingEvent {
  CombatantRef who;
  std::string moveName;
  int ppSpent = 0; // PP paid here: a two-turn move pays on its charge turn
};

struct ProtectedEvent {
  CombatantRef who;
};

using BattleEvent =
    std::variant<MoveUsedEvent, DamageDealtEvent, FaintedEvent, MissedEvent, StatusAppliedEvent,
                 StatusFailedEvent, StatusDamageEvent, StatusCuredEvent, MoveSkippedEvent,
                 StatStageChangedEvent, StatChangeFailedEvent, SwitchedOutEvent, SwitchedInEvent,
                 AbilityTriggeredEvent, MoveFailedEvent, WeatherStartedEvent, WeatherEndedEvent,
                 WeatherDamageEvent, HazardSetEvent, HazardDamageEvent, HazardsClearedEvent,
                 ToxicSpikesAbsorbedEvent, HealedEvent, RecoilDamageEvent, ChargingEvent,
                 ProtectedEvent, ItemTriggeredEvent, ItemConsumedEvent, ItemDamageEvent,
                 AbilityDamageEvent,
                 ItemKnockedOffEvent, TerrainStartedEvent, TerrainEndedEvent, ScreenStartedEvent,
                 ScreenEndedEvent, DestinyBondTriggeredEvent>;
using EventLog = std::vector<BattleEvent>;
} // namespace engine
