#pragma once

#include "engine/core/battle_state.hpp"
#include "engine/core/events.hpp"
#include "engine/ffi/ffi.hpp"

#include <vector>

namespace engine::ffi {
// Throws E_ACTION on an unknown kind.
Action toAction(const FfiAction &action);

std::vector<FfiEvent> flatten(const EventLog &log);
} // namespace engine::ffi
