#pragma once

#include "engine/core/battle_state.hpp"
#include "engine/model/pokemon.hpp"

#include <array>

namespace engine {

struct BattleState;
class DataLoader;

// Must be called at the start of every FFI-exposed function (ADR #13).
// Throws std::invalid_argument with a descriptive message on any violation.
// Each phase that adds fields to BattleState must extend this function.
void validateState(const BattleState &state, const DataLoader &data);

// Team legality: Species Clause, at most one Mega, at most one legendary, and
// every move drawn from the species' own movepool.
//
// Deliberately NOT called by validateState (ADR #50): these are BUILD rules,
// not state invariants. A battle already under way with an illegal team is not
// corrupt — it should have been refused at submission. Keeping them apart
// spares every turn a check that belongs to the teambuilder, which calls this
// once across the FFI before the battle starts.
//
// Throws std::invalid_argument with a descriptive message on any violation.
void validateTeam(const std::array<BattlePokemon, kTeamSize> &team, int teamSize,
                  const DataLoader &data);

} // namespace engine
