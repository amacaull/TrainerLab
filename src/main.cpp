#include "engine/battle_state.hpp"
#include "engine/data_loader.hpp"
#include "engine/engine.hpp"
#include "engine/events.hpp"
#include "engine/rng.hpp"
#include "engine/validate.hpp"

#include <iostream>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

using namespace engine;

namespace {

BattlePokemon buildCombatant(const DataLoader& data,
                             const std::string& speciesName,
                             int level,
                             const std::vector<std::string>& moveNames) {
    BattlePokemon p;
    p.species_id = data.findSpeciesId(speciesName);
    if (p.species_id < 0) throw std::runtime_error("Unknown species: " + speciesName);

    const Species& sp = data.speciesByIndex(p.species_id);
    p.level = level;
    p.stats = computeStats(sp.baseStats, level);
    p.currentHp = p.stats.hp;

    for (size_t i = 0; i < moveNames.size() && i < kMaxMovesPerPokemon; ++i) {
        int mid = data.findMoveId(moveNames[i]);
        if (mid < 0) throw std::runtime_error("Unknown move: " + moveNames[i]);
        p.move_ids[i] = mid;
    }
    return p;
}

void printEvent(const BattleEvent& ev, const BattleState& state, const DataLoader& data) {
    auto pokeName = [&](const CombatantRef& r) {
        const BattlePokemon& p = state.teams[static_cast<size_t>(r.side)][static_cast<size_t>(r.teamIndex)];
        return data.speciesByIndex(p.species_id).displayName;
    };

    std::visit([&](auto&& e) {
        using T = std::decay_t<decltype(e)>;
        if constexpr (std::is_same_v<T, MoveUsedEvent>) {
            std::cout << "    " << pokeName(e.user) << " uses " << e.moveName << "!\n";
        } else if constexpr (std::is_same_v<T, DamageDealtEvent>) {
            std::cout << "    " << pokeName(e.target) << " takes " << e.damage << " damage";
            if (e.wasStab) std::cout << " (STAB)";
            if      (e.effectiveness == 0.0f) std::cout << " (no effect)";
            else if (e.effectiveness >= 4.0f) std::cout << " (4x super effective!)";
            else if (e.effectiveness >= 2.0f) std::cout << " (super effective!)";
            else if (e.effectiveness <= 0.25f) std::cout << " (4x resisted)";
            else if (e.effectiveness < 1.0f)  std::cout << " (not very effective)";
            std::cout << "\n";
        } else if constexpr (std::is_same_v<T, FaintedEvent>) {
            std::cout << "    " << pokeName(e.who) << " fainted!\n";
        } else if constexpr (std::is_same_v<T, MissedEvent>) {
            std::cout << "    " << pokeName(e.user) << " missed " << e.moveName << "!\n";
        }
    }, ev);
}

struct Scenario {
    const char* title;
    const char* showcases;
    const char* species0;
    std::vector<std::string> moves0;
    const char* species1;
    std::vector<std::string> moves1;
    uint64_t seed;
    int maxTurns;
};

void runMatch(const Scenario& sc, const DataLoader& data, const BattleEngine& engine) {
    BattleState state;
    state.teams[0][0] = buildCombatant(data, sc.species0, 50, sc.moves0);
    state.teams[1][0] = buildCombatant(data, sc.species1, 50, sc.moves1);
    state.team_size = {1, 1};
    state.activeIndex = {0, 0};
    validateState(state, data);

    const Species& sp0 = data.speciesByIndex(state.teams[0][0].species_id);
    const Species& sp1 = data.speciesByIndex(state.teams[1][0].species_id);

    std::cout << "=== " << sc.title << " ===\n";
    std::cout << "    Showcases: " << sc.showcases << "\n";
    std::cout << "    " << sp0.displayName << " vs " << sp1.displayName << "\n\n";

    MersenneRNG rng(sc.seed);
    int turn = 0;
    while (!state.isOver() && turn < sc.maxTurns) {
        ++turn;
        std::cout << "  Turn " << turn << "  [HP " << sp0.displayName << " "
                  << state.teams[0][0].currentHp << "/" << state.teams[0][0].stats.hp
                  << " | " << sp1.displayName << " "
                  << state.teams[1][0].currentHp << "/" << state.teams[1][0].stats.hp << "]\n";
        auto events = engine.resolveTurn(state, UseMove{0}, UseMove{0}, rng);
        for (const auto& e : events) printEvent(e, state, data);
        std::cout << "\n";
    }

    std::cout << "  Outcome: ";
    if      (state.sideHasLost(0)) std::cout << sp1.displayName << " wins";
    else if (state.sideHasLost(1)) std::cout << sp0.displayName << " wins";
    else                           std::cout << "unresolved after " << turn << " turns";
    std::cout << " (" << state.turn << " turn" << (state.turn > 1 ? "s" : "") << ")\n\n";
}

} // namespace

int main() {
    try {
        DataLoader data;
        data.loadAll(BATTLE_ENGINE_DATA_DIR);

        std::cout << "Battle engine demo  -  "
                  << data.speciesCount() << " species, "
                  << data.moveCount() << " moves loaded\n\n";

        const std::vector<Scenario> scenarios = {
            { "Match 1: STAB super-effective",
              "STAB (x1.5) + super-effective (x2) on a Grass/Poison target",
              "charizard", {"Flamethrower"},
              "venusaur",  {"VineWhip"},
              42, 10 },

            { "Match 2: 4x weakness via dual type",
              "Rock vs Fire/Flying = 2x * 2x = 4x; one-shot OHKO",
              "garchomp",  {"StoneEdge"},
              "charizard", {"AirSlash"},
              42, 5 },

            { "Match 3: priority bracket beats raw speed",
              "Pikachu (Spd 95) uses QuickAttack (+1 priority) before Gengar (Spd 115); Normal hits Ghost for 0",
              "pikachu", {"QuickAttack"},
              "gengar",  {"ShadowBall"},
              42, 3 },

            { "Match 4: accuracy roll, miss event",
              "Machamp's StoneEdge has 80% accuracy; seed 2 forces a miss on turn 1",
              "machamp",   {"StoneEdge"},
              "blastoise", {"Surf"},
              2, 5 },

            { "Match 5: bulk vs frailty",
              "Snorlax (220 HP, 115 SpD) sponges a Thunderbolt; Pikachu (95 HP, 45 Def) folds to BodySlam",
              "snorlax", {"BodySlam"},
              "pikachu", {"Thunderbolt"},
              7, 5 },
        };

        BattleEngine engine(data);
        for (const auto& sc : scenarios) {
            runMatch(sc, data, engine);
        }

        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }
}
