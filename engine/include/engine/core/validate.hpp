#pragma once

#include "engine/core/battle_state.hpp"
#include "engine/model/pokemon.hpp"

#include <array>

namespace engine {
struct BattleState;
class DataLoader;

// Called at the start of every FFI function.
void validateState(const BattleState &state, const DataLoader &data);

// Not called by validateState: these are build rules checked once at submission, not state
// invariants.
void validateTeam(const std::array<BattlePokemon, kTeamSize> &team, int teamSize,
                  const DataLoader &data);
} // namespace engine
