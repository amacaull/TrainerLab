#pragma once

#include "engine/field.hpp"
#include "engine/pokemon.hpp"

#include <array>
#include <variant>

namespace engine {

constexpr int kTeamSize = 6; // 3v3 and 6v6 formats: team_size fills partially (ADR #36)
constexpr int kSideCount = 2;

struct UseMove {
  int moveIndex;
  // Pivot moves (U-Turn...): team index to switch to, declared upfront
  // (ADR #20). -1 = auto (first healthy benched teammate).
  int pivotTarget = -1;
};

struct SwitchAction {
  int teamIndex;
};

using Action = std::variant<UseMove, SwitchAction>;

// FFI contract: POD layout, fixed-size teams, no heap indirection.
// team_size[side] indicates how many slots are filled; trailing slots
// stay at BattlePokemon defaults (species_id == kNoSpecies).
struct BattleState {
  std::array<std::array<BattlePokemon, kTeamSize>, kSideCount> teams{};
  std::array<int, kSideCount> team_size{0, 0};
  std::array<int, kSideCount> activeIndex{0, 0};
  int turn = 0;

  Weather weather = Weather::None;
  int weather_turns_left = 0;
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

} // namespace engine
