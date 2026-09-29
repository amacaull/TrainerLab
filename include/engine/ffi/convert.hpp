#pragma once

#include "engine/core/battle_state.hpp"
#include "engine/core/events.hpp"
#include "engine/ffi/ffi.hpp"

#include <vector>

// Flattening layer. std::variant has no guaranteed layout and std::string
// cannot cross, so the type of an event becomes a number and its fields become
// numbered slots. The event table in README.md section 6 defines what each
// slot means per kind, and that table is what this file implements.
//
// No game logic lives here: this layer only reshapes what the engine
// produced, so every caller sees the same battle.
namespace engine::ffi {

// Rebuilds the variant from the tag. Throws E_ACTION on an unknown kind - the
// one failure mode a C++ variant cannot have, and therefore the only one the
// flattening introduces.
Action toAction(const FfiAction &action);

std::vector<FfiEvent> flatten(const EventLog &log);

} // namespace engine::ffi
