#include "engine/core/observation.hpp"

#include "engine/model/status.hpp"

namespace engine {
BattleState observe(const BattleState &state, int side) {
  BattleState view = state;
  for (int s = 0; s < kSideCount; ++s) {
    for (int i = 0; i < kTeamSize; ++i) {
      BattlePokemon &p = view.teams[static_cast<size_t>(s)][static_cast<size_t>(i)];
      if (s != side && p.revealed == 0) {
        p = BattlePokemon{};
        continue;
      }
      if (p.status == Status::Sleep && p.sleep_self_inflicted == 0)
        p.status_turns = 0;
    }
  }
  return view;
}
} // namespace engine
