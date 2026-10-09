#pragma once

#include "engine/core/battle_state.hpp"

namespace engine {
// What the player on `side` can see. Two things are hidden, from both sides alike:
// - an opponent that never entered the field: its slot is reset to BattlePokemon{}, team_size
//   stays (the player knows how many Pokemon remain to be revealed);
// - the remaining sleep turns of a non-Rest sleep (status_turns reads 0). Rest always sleeps two
//   turns, so its counter is public knowledge and stays.
// Everything else is visible, exact HP included: the events show exact damage, and sets are fixed
// per species in a public catalog. The result is a view, not a playable state: validateState
// rejects the reset slots, so it never goes back into the engine.
BattleState observe(const BattleState &state, int side);
} // namespace engine
