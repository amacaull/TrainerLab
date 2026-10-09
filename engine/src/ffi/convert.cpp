#include "engine/ffi/convert.hpp"

#include "engine/abilities/ability.hpp"
#include "engine/core/data_loader.hpp"
#include "engine/items/item.hpp"

#include <stdexcept>
#include <string>
#include <type_traits>
#include <variant>

namespace engine::ffi {
namespace {
// Kind numbers come from the README.md section 6 table, NOT from the order of the std::variant: the
// two diverge.
enum Kind : uint8_t {
  kMoveUsed = 0,
  kDamageDealt = 1,
  kFainted = 2,
  kMissed = 3,
  kStatusApplied = 4,
  kStatusFailed = 5,
  kStatusDamage = 6,
  kStatusCured = 7,
  kMoveSkipped = 8,
  kStatStageChanged = 9,
  kStatChangeFailed = 10,
  kSwitchedOut = 11,
  kSwitchedIn = 12,
  kAbilityTriggered = 13,
  kMoveFailed = 14,
  kWeatherStarted = 15,
  kWeatherEnded = 16,
  kWeatherDamage = 17,
  kHazardSet = 18,
  kHazardDamage = 19,
  kHazardsCleared = 20,
  kToxicSpikesAbsorbed = 21,
  kHealed = 22,
  kRecoilDamage = 23,
  kCharging = 24,
  kProtected = 25,
  kItemTriggered = 26,
  kItemConsumed = 27,
  kItemDamage = 28,
  kItemKnockedOff = 29,
  kTerrainStarted = 30,
  kTerrainEnded = 31,
  kScreenStarted = 32,
  kScreenEnded = 33,
  kDestinyBondTriggered = 34,
  kAbilityDamage = 35,
};

FfiEvent make(uint8_t kind) {
  FfiEvent e{};
  e.kind = kind;
  return e;
}

FfiEvent make(uint8_t kind, const CombatantRef &who) {
  FfiEvent e = make(kind);
  e.side = static_cast<int8_t>(who.side);
  e.slot = static_cast<int8_t>(who.teamIndex);
  return e;
}

FfiEvent makeSide(uint8_t kind, int side) {
  FfiEvent e = make(kind);
  e.side = static_cast<int8_t>(side);
  return e;
}

// A name with no catalog id is an internal inconsistency, not user input: throw rather than degrade
// to a silent -1.
int requireMoveId(const std::string &name) {
  if (name == "Struggle")
    return kFfiStruggle;
  int id = find_move_id(name);
  if (id < 0)
    throw std::out_of_range("E_ARG: event names move '" + name + "' which is not in the catalog");
  return id;
}

int requireItemId(const std::string &name) {
  int id = find_item_id(name);
  if (id < 0)
    throw std::out_of_range("E_ARG: event names item '" + name + "' which is not in the catalog");
  return id;
}

int requireAbilityId(const std::string &name) {
  if (name == "StruggleRecoil")
    return kFfiStruggleRecoil;
  int id = find_ability_id(name);
  if (id < 0)
    throw std::out_of_range("E_ARG: event names ability '" + name +
                            "' which is not in the catalog");
  return id;
}

template <typename E> int asInt(E value) { return static_cast<int>(value); }

template <typename> inline constexpr bool alwaysFalse = false;

FfiEvent flattenOne(const BattleEvent &ev) {
  return std::visit(
      [](auto &&e) -> FfiEvent {
        using T = std::decay_t<decltype(e)>;

        if constexpr (std::is_same_v<T, MoveUsedEvent>) {
          FfiEvent f = make(kMoveUsed, e.user);
          f.name_id = requireMoveId(e.moveName);
          f.i0 = e.ppSpent;
          return f;
        } else if constexpr (std::is_same_v<T, DamageDealtEvent>) {
          FfiEvent f = make(kDamageDealt, e.target);
          f.i0 = e.damage;
          f.f0 = e.effectiveness;
          f.flags = static_cast<uint8_t>((e.wasStab ? kFfiFlagStab : 0) |
                                         (e.wasCrit ? kFfiFlagCrit : 0));
          return f;
        } else if constexpr (std::is_same_v<T, FaintedEvent>) {
          return make(kFainted, e.who);
        } else if constexpr (std::is_same_v<T, MissedEvent>) {
          FfiEvent f = make(kMissed, e.user);
          f.name_id = requireMoveId(e.moveName);
          return f;
        } else if constexpr (std::is_same_v<T, StatusAppliedEvent>) {
          FfiEvent f = make(kStatusApplied, e.target);
          f.i0 = asInt(e.status);
          return f;
        } else if constexpr (std::is_same_v<T, StatusFailedEvent>) {
          FfiEvent f = make(kStatusFailed, e.target);
          f.i0 = asInt(e.status);
          return f;
        } else if constexpr (std::is_same_v<T, StatusDamageEvent>) {
          FfiEvent f = make(kStatusDamage, e.target);
          f.i0 = e.damage;
          f.i1 = asInt(e.status);
          return f;
        } else if constexpr (std::is_same_v<T, StatusCuredEvent>) {
          FfiEvent f = make(kStatusCured, e.who);
          f.i0 = asInt(e.status);
          return f;
        } else if constexpr (std::is_same_v<T, MoveSkippedEvent>) {
          FfiEvent f = make(kMoveSkipped, e.user);
          f.i0 = asInt(e.reason);
          return f;
        } else if constexpr (std::is_same_v<T, StatStageChangedEvent>) {
          FfiEvent f = make(kStatStageChanged, e.target);
          f.i0 = e.delta; // the delta applied, not the resulting stage
          f.i1 = asInt(e.stat);
          return f;
        } else if constexpr (std::is_same_v<T, StatChangeFailedEvent>) {
          FfiEvent f = make(kStatChangeFailed, e.target);
          f.i0 = asInt(e.stat);
          f.i1 = e.wasRaise ? 1 : 0;
          return f;
        } else if constexpr (std::is_same_v<T, SwitchedOutEvent>) {
          return make(kSwitchedOut, e.who);
        } else if constexpr (std::is_same_v<T, SwitchedInEvent>) {
          return make(kSwitchedIn, e.who);
        } else if constexpr (std::is_same_v<T, AbilityTriggeredEvent>) {
          FfiEvent f = make(kAbilityTriggered, e.who);
          f.name_id = requireAbilityId(e.ability);
          return f;
        } else if constexpr (std::is_same_v<T, MoveFailedEvent>) {
          FfiEvent f = make(kMoveFailed, e.user);
          f.name_id = requireMoveId(e.moveName);
          return f;
        } else if constexpr (std::is_same_v<T, WeatherStartedEvent>) {
          FfiEvent f = make(kWeatherStarted);
          f.i0 = asInt(e.weather);
          return f;
        } else if constexpr (std::is_same_v<T, WeatherEndedEvent>) {
          FfiEvent f = make(kWeatherEnded);
          f.i0 = asInt(e.weather);
          return f;
        } else if constexpr (std::is_same_v<T, WeatherDamageEvent>) {
          FfiEvent f = make(kWeatherDamage, e.target);
          f.i0 = e.damage;
          f.i1 = asInt(e.weather);
          return f;
        } else if constexpr (std::is_same_v<T, HazardSetEvent>) {
          FfiEvent f = makeSide(kHazardSet, e.side);
          f.i0 = asInt(e.hazard);
          f.i1 = e.layers;
          return f;
        } else if constexpr (std::is_same_v<T, HazardDamageEvent>) {
          FfiEvent f = make(kHazardDamage, e.target);
          f.i0 = e.damage;
          f.i1 = asInt(e.hazard);
          return f;
        } else if constexpr (std::is_same_v<T, HazardsClearedEvent>) {
          return makeSide(kHazardsCleared, e.side);
        } else if constexpr (std::is_same_v<T, ToxicSpikesAbsorbedEvent>) {
          return make(kToxicSpikesAbsorbed, e.who);
        } else if constexpr (std::is_same_v<T, HealedEvent>) {
          FfiEvent f = make(kHealed, e.who);
          f.i0 = e.amount;
          return f;
        } else if constexpr (std::is_same_v<T, RecoilDamageEvent>) {
          FfiEvent f = make(kRecoilDamage, e.who);
          f.i0 = e.damage;
          return f;
        } else if constexpr (std::is_same_v<T, ChargingEvent>) {
          FfiEvent f = make(kCharging, e.who);
          f.name_id = requireMoveId(e.moveName);
          f.i0 = e.ppSpent;
          return f;
        } else if constexpr (std::is_same_v<T, ProtectedEvent>) {
          return make(kProtected, e.who);
        } else if constexpr (std::is_same_v<T, ItemTriggeredEvent>) {
          FfiEvent f = make(kItemTriggered, e.who);
          f.name_id = requireItemId(e.itemName);
          return f;
        } else if constexpr (std::is_same_v<T, ItemConsumedEvent>) {
          FfiEvent f = make(kItemConsumed, e.who);
          f.name_id = requireItemId(e.itemName);
          return f;
        } else if constexpr (std::is_same_v<T, ItemDamageEvent>) {
          FfiEvent f = make(kItemDamage, e.who);
          f.name_id = requireItemId(e.itemName);
          f.i0 = e.damage;
          return f;
        } else if constexpr (std::is_same_v<T, AbilityDamageEvent>) {
          FfiEvent f = make(kAbilityDamage, e.who);
          f.name_id = requireAbilityId(e.ability);
          f.i0 = e.damage;
          return f;
        } else if constexpr (std::is_same_v<T, ItemKnockedOffEvent>) {
          FfiEvent f = make(kItemKnockedOff, e.who);
          f.name_id = requireItemId(e.itemName);
          return f;
        } else if constexpr (std::is_same_v<T, TerrainStartedEvent>) {
          FfiEvent f = make(kTerrainStarted);
          f.i0 = asInt(e.terrain);
          return f;
        } else if constexpr (std::is_same_v<T, TerrainEndedEvent>) {
          FfiEvent f = make(kTerrainEnded);
          f.i0 = asInt(e.terrain);
          return f;
        } else if constexpr (std::is_same_v<T, ScreenStartedEvent>) {
          FfiEvent f = makeSide(kScreenStarted, e.side);
          f.i0 = e.turns;
          return f;
        } else if constexpr (std::is_same_v<T, ScreenEndedEvent>) {
          return makeSide(kScreenEnded, e.side);
        } else if constexpr (std::is_same_v<T, DestinyBondTriggeredEvent>) {
          return make(kDestinyBondTriggered, e.dragged);
        } else {
          // A new event type reaches the boundary with no mapping: fail to
          // compile rather than emit kind 0 at runtime.
          static_assert(alwaysFalse<T>, "unmapped event type in flatten()");
        }
      },
      ev);
}
} // namespace

Action toAction(const FfiAction &action) {
  switch (action.kind) {
  case 0:
    return UseMove{action.index, action.pivot_target};
  case 1:
    return SwitchAction{action.index};
  default:
    throw std::invalid_argument("E_ACTION:BAD_KIND: unknown action kind " +
                                std::to_string(static_cast<int>(action.kind)));
  }
}

FfiAction toFfiAction(const Action &action) {
  if (const auto *sw = std::get_if<SwitchAction>(&action))
    return FfiAction{1, sw->teamIndex, -1};
  const auto &move = std::get<UseMove>(action);
  return FfiAction{0, move.moveIndex, move.pivotTarget};
}

std::vector<FfiEvent> flatten(const EventLog &log) {
  std::vector<FfiEvent> out;
  out.reserve(log.size());
  for (const BattleEvent &ev : log)
    out.push_back(flattenOne(ev));
  return out;
}
} // namespace engine::ffi
