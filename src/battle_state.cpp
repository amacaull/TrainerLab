#include "engine/battle_state.hpp"

#include <algorithm>

namespace engine {

bool BattleState::sideHasLost(int side) const {
    const auto& team = teams[static_cast<size_t>(side)];
    return std::all_of(team.begin(), team.end(), [](const BattlePokemon& p) { return p.isFainted(); });
}

} // namespace engine
