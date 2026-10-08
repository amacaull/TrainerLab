#pragma once

#include "engine/model/move.hpp"

namespace engine {
// Struggle is a rule, not content: the one move the engine knows by name, and the only one that
// never lives in data/.
const Move &struggleMove();
} // namespace engine
