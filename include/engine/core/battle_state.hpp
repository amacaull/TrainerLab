#pragma once

#include "engine/model/field.hpp"
#include "engine/model/pokemon.hpp"

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
  Terrain terrain = Terrain::None;
  int terrain_turns_left = 0;
  std::array<int, 2> aurora_veil_turns{0, 0}; // per side; 0 = no screen (ADR #39)
  std::array<int, 2> wish_turns{0, 0};        // Wish: 2 at cast, heals when reaching 0
  std::array<int, 2> wish_heal{0, 0};         // amount = caster's max HP / 2
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
