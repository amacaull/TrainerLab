#pragma once

#include "engine/battle_state.hpp"
#include "engine/data_loader.hpp"
#include "engine/events.hpp"
#include "engine/rng.hpp"

namespace engine {

// Stateless: holds no battle state of its own.
class BattleEngine {
public:
    explicit BattleEngine(const DataLoader& data) : data_(data) {}

    // Resolves one turn in-place on state. Returns the event log for the turn.
    EventLog resolveTurn(BattleState& state, const Action& actionP0, const Action& actionP1, RNG& rng) const;

private:
    // Returns {first_side, second_side} based on priority then speed.
    std::array<int, 2> computeOrder(const BattleState& state, const Action& a0, const Action& a1) const;

    void executeAction(BattleState& state, int side, const Action& action, RNG& rng, EventLog& events) const;

    const DataLoader& data_;
};

} // namespace engine
