#include "engine/battle_state.hpp"

namespace engine {

bool BattleState::sideHasLost(int side) const {
  int size = team_size[static_cast<size_t>(side)];
  if (size <= 0)
    return true;
  const auto &team = teams[static_cast<size_t>(side)];
  for (int i = 0; i < size; ++i) {
    if (!team[static_cast<size_t>(i)].isFainted())
      return false;
  }
  return true;
}

} // namespace engine
