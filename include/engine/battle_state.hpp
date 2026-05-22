#pragma once

#include "engine/pokemon.hpp"

#include <array>
#include <variant>
#include <vector>

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

// POD-only: no methods beyond getters. Future-proof for FFI exposure.
struct BattleState {
    std::array<std::vector<BattlePokemon>, kSideCount> teams;
    std::array<int, kSideCount> activeIndex {0, 0};
    int turn = 0;

    const BattlePokemon& active(int side) const {
        return teams[static_cast<size_t>(side)][static_cast<size_t>(activeIndex[static_cast<size_t>(side)])];
    }

    BattlePokemon& active(int side) {
        return teams[static_cast<size_t>(side)][static_cast<size_t>(activeIndex[static_cast<size_t>(side)])];
    }

    bool sideHasLost(int side) const;
    bool isOver() const { return sideHasLost(0) || sideHasLost(1); }
};

} // namespace engine
