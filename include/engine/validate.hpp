#pragma once

namespace engine {

struct BattleState;
class DataLoader;

// Must be called at the start of every FFI-exposed function (ADR #13).
// Throws std::invalid_argument with a descriptive message on any violation.
// Each phase that adds fields to BattleState must extend this function.
void validateState(const BattleState& state, const DataLoader& data);

} // namespace engine
