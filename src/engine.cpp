#include "engine/engine.hpp"

#include "engine/move.hpp"
#include "engine/pokemon.hpp"

#include <variant>

namespace engine {

namespace {

// Switch actions always go first (priority +6).
int actionPriority(const Action& a, const DataLoader& data, const BattlePokemon& user) {
    if (std::holds_alternative<SwitchAction>(a)) return 6;
    const auto& useMove = std::get<UseMove>(a);
    const std::string& moveName = user.moves[static_cast<size_t>(useMove.moveIndex)];
    return data.move(moveName).priority;
}

} // namespace

std::array<int, 2> BattleEngine::computeOrder(const BattleState& state, const Action& a0, const Action& a1) const {
    int p0 = actionPriority(a0, data_, state.active(0));
    int p1 = actionPriority(a1, data_, state.active(1));
    if (p0 != p1) return (p0 > p1) ? std::array<int, 2>{0, 1} : std::array<int, 2>{1, 0};

    int s0 = state.active(0).stats.speed;
    int s1 = state.active(1).stats.speed;
    if (s0 != s1) return (s0 > s1) ? std::array<int, 2>{0, 1} : std::array<int, 2>{1, 0};

    // Speed tie: side 0 first for now (phase 1 will make this random).
    return {0, 1};
}

void BattleEngine::executeAction(BattleState& state, int side, const Action& action, RNG& rng, EventLog& events) const {
    // Attacker may have fainted from the previous action this turn.
    if (state.active(side).isFainted()) return;

    // Phase 0: switch not implemented yet, silently skip.
    if (std::holds_alternative<SwitchAction>(action)) return;

    const auto& useMove = std::get<UseMove>(action);
    BattlePokemon& user = state.active(side);
    const std::string& moveName = user.moves[static_cast<size_t>(useMove.moveIndex)];
    const Move& move = data_.move(moveName);

    CombatantRef userRef{side, state.activeIndex[static_cast<size_t>(side)]};
    int otherSide = 1 - side;
    CombatantRef targetRef{otherSide, state.activeIndex[static_cast<size_t>(otherSide)]};

    events.emplace_back(MoveUsedEvent{userRef, move.name});

    if (move.accuracy < 100 && !rng.chancePct(move.accuracy)) {
        events.emplace_back(MissedEvent{userRef, move.name});
        return;
    }

    EffectContext ctx{state, rng, events, userRef, targetRef, move};
    for (const auto& effect : move.effects) {
        effect->apply(ctx);
    }
}

EventLog BattleEngine::resolveTurn(BattleState& state, const Action& a0, const Action& a1, RNG& rng) const {
    EventLog events;
    state.turn += 1;

    auto order = computeOrder(state, a0, a1);
    const Action* actions[2] = {&a0, &a1};

    for (int side : order) {
        if (state.isOver()) break;
        executeAction(state, side, *actions[side], rng, events);
    }
    return events;
}

} // namespace engine
