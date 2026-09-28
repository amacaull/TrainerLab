#pragma once

#include "engine/model/field.hpp"
#include "engine/model/pokemon.hpp"
#include "engine/model/status.hpp"

#include <string>
#include <variant>
#include <vector>

namespace engine {

struct CombatantRef {
  int side;      // 0 or 1
  int teamIndex; // 0..kTeamSize-1
};

// Crosses the FFI boundary as int inside FfiEvent. Do not reorder.
enum class SkipReason : int { Asleep = 0, Frozen, FullyParalyzed, Flinched };

struct MoveUsedEvent {
  CombatantRef user;
  std::string moveName;
  int ppSpent = 0; // 2 against Pressure; 0 for a release, Struggle or a called move
};

struct DamageDealtEvent {
  CombatantRef target;
  int damage;
  float effectiveness; // 0.0, 0.5, 1.0, 2.0, 4.0
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

struct SwitchedOutEvent {
  CombatantRef who;
};

struct SwitchedInEvent {
  CombatantRef who;
};

// Intimidate announce, Levitate blocking a Ground move, etc.
struct AbilityTriggeredEvent {
  CombatantRef who;
  std::string ability;
};

// Generic "But it failed!": weather already active, hazard at max layers.
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
  int side; // side whose field gains the hazard
  HazardKind hazard;
  int layers; // total layers after the set
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

// Recoil moves and contact punishment (Rough Skin).
struct RecoilDamageEvent {
  CombatantRef who;
  int damage;
};

struct TerrainStartedEvent { // posed for phase 13 (Createur Electrik)
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
  CombatantRef dragged; // the attacker taken along
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

// Self-damage attributed to an ability (Disguise's 1/8 chip). Kept apart from
// ItemDamageEvent: at the FFI boundary the name resolves against a different
// catalog, so the two must not share a payload (ADR #45, ADR #12).
struct AbilityDamageEvent {
  CombatantRef who;
  std::string ability;
  int damage;
};

// Posed for Sabotage (phase 14): the item is stripped from its holder.
struct ItemKnockedOffEvent {
  CombatantRef who;
  std::string itemName;
};

// First turn of a two-turn move (Fly, Dig, SolarBeam charge).
struct ChargingEvent {
  CombatantRef who;
  std::string moveName;
  int ppSpent = 0; // a two-turn move pays on its charge turn
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
