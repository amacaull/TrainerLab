#pragma once

#include "engine/pokemon.hpp"

#include <array>
#include <variant>

namespace engine {

constexpr int kTeamSize = 3;
constexpr int kSideCount = 2;

struct UseMove {
  int moveIndex;
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
