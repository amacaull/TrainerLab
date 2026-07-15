#pragma once

#include "engine/move.hpp"

namespace engine {

// Lutte (Struggle) is a game *rule*, not content: the mandatory fallback
// when no PP is left. It lives in code, never in data/ — the one exception
// to "the engine knows no move by name" (ADR #35).
// Canon: physical, 50 BP, typeless (x1 vs everything, no STAB), never
// misses, costs no PP, 25% max-HP recoil on the user if it hits.
const Move &struggleMove();

} // namespace engine
