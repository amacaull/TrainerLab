#pragma once

#include "engine/model/field.hpp"
#include "engine/model/pokemon.hpp"

#include <array>
#include <variant>

namespace engine {
constexpr int kTeamSize = 6;
constexpr int kSideCount = 2;

struct UseMove {
  int moveIndex;
  // -1 = auto: the first healthy benched teammate.
  int pivotTarget = -1;
};

struct SwitchAction {
  int teamIndex;
};

using Action = std::variant<UseMove, SwitchAction>;

// FFI contract: POD, fixed size, no heap. Slots past team_size keep the BattlePokemon defaults.
struct BattleState {
  std::array<std::array<BattlePokemon, kTeamSize>, kSideCount> teams{};
  std::array<int, kSideCount> team_size{0, 0};
  std::array<int, kSideCount> activeIndex{0, 0};
  int turn = 0;

  Weather weather = Weather::None;
  int weather_turns_left = 0;
  Terrain terrain = Terrain::None;
  int terrain_turns_left = 0;
  std::array<int, 2> aurora_veil_turns{0, 0};
  std::array<int, 2> wish_turns{0, 0};
  std::array<int, 2> wish_heal{0, 0};
  std::array<SideHazards, kSideCount> hazards{};

  const BattlePokemon &active(int side) const {
    return teams[static_cast<size_t>(side)]
                [static_cast<size_t>(activeIndex[static_cast<size_t>(side)])];
  }

  BattlePokemon &active(int side) {
    return teams[static_cast<size_t>(side)]
                [static_cast<size_t>(activeIndex[static_cast<size_t>(side)])];
  }

  bool sideHasLost(int side) const;
  bool isOver() const { return sideHasLost(0) || sideHasLost(1); }
};

// EmergencyExit can swap a Pokemon out mid-move: re-check before reusing a CombatantRef captured
// before the damage landed.
inline bool stillOnField(const BattleState &state, int side, int teamIndex) {
  return state.activeIndex[static_cast<size_t>(side)] == teamIndex;
}
} // namespace engine
