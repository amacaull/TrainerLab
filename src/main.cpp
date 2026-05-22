// Demo: 1v1 Charizard (Flamethrower) vs Venusaur (VineWhip).
// Prints the event log to stdout.

#include "engine/battle_state.hpp"
#include "engine/data_loader.hpp"
#include "engine/engine.hpp"
#include "engine/events.hpp"
#include "engine/rng.hpp"

#include <iostream>
#include <variant>

using namespace engine;

namespace {

BattlePokemon buildCombatant(const Species& sp, int level, const std::vector<std::string>& moves) {
    BattlePokemon p;
    p.species = &sp;
    p.level = level;
    p.stats = computeStats(sp.baseStats, level);
    p.currentHp = p.stats.hp;
    p.moves = moves;
    return p;
}

void printEvent(const BattleEvent& ev, const BattleState& state) {
    auto pokeName = [&](const CombatantRef& r) {
        return state.teams[static_cast<size_t>(r.side)][static_cast<size_t>(r.teamIndex)].species->displayName;
    };

    std::visit([&](auto&& e) {
        using T = std::decay_t<decltype(e)>;
        if constexpr (std::is_same_v<T, MoveUsedEvent>) {
            std::cout << "  " << pokeName(e.user) << " uses " << e.moveName << "!\n";
        } else if constexpr (std::is_same_v<T, DamageDealtEvent>) {
            std::cout << "  " << pokeName(e.target) << " takes " << e.damage << " damage";
            if (e.wasStab) std::cout << " (STAB)";
            if (e.effectiveness == 0.0f) std::cout << " (no effect)";
            else if (e.effectiveness >= 2.0f) std::cout << " (super effective!)";
            else if (e.effectiveness < 1.0f) std::cout << " (not very effective)";
            std::cout << "\n";
        } else if constexpr (std::is_same_v<T, FaintedEvent>) {
            std::cout << "  " << pokeName(e.who) << " fainted!\n";
        } else if constexpr (std::is_same_v<T, MissedEvent>) {
            std::cout << "  " << pokeName(e.user) << " missed " << e.moveName << "!\n";
        }
    }, ev);
}

} // namespace

int main() {
    try {
        DataLoader data;
        data.loadAll(BATTLE_ENGINE_DATA_DIR);

        const Species& charizard = data.species("charizard");
        const Species& venusaur  = data.species("venusaur");

        BattleState state;
        state.teams[0].push_back(buildCombatant(charizard, 50, {"Flamethrower", "Tackle"}));
        state.teams[1].push_back(buildCombatant(venusaur,  50, {"VineWhip",     "Tackle"}));
        state.activeIndex = {0, 0};

        std::cout << "=== Battle: " << charizard.displayName << " vs " << venusaur.displayName << " ===\n\n";

        BattleEngine engine(data);
        MersenneRNG rng(42);

        int safety = 50;
        while (!state.isOver() && safety-- > 0) {
            std::cout << "Turn " << (state.turn + 1) << "\n";
            std::cout << "  HP: " << charizard.displayName << " " << state.teams[0][0].currentHp << "/" << state.teams[0][0].stats.hp
                      << " | " << venusaur.displayName << " " << state.teams[1][0].currentHp << "/" << state.teams[1][0].stats.hp << "\n";

            Action a0 = UseMove{0};
            Action a1 = UseMove{0};
            auto events = engine.resolveTurn(state, a0, a1, rng);
            for (const auto& e : events) printEvent(e, state);
            std::cout << "\n";
        }

        std::cout << "=== Battle over ===\n";
        if      (state.sideHasLost(0)) std::cout << "Side 1 (" << venusaur.displayName  << ") wins!\n";
        else if (state.sideHasLost(1)) std::cout << "Side 0 (" << charizard.displayName << ") wins!\n";
        else                           std::cout << "Draw or unresolved.\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }
}
