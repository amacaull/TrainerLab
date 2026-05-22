#include "engine/battle_state.hpp"
#include "engine/data_loader.hpp"
#include "engine/engine.hpp"
#include "engine/rng.hpp"

#include <catch2/catch_test_macros.hpp>

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

} // namespace

TEST_CASE("Full battle ends in a KO", "[engine][integration]") {
    DataLoader data;
    data.loadAll(BATTLE_ENGINE_DATA_DIR);

    BattleState state;
    state.teams[0].push_back(buildCombatant(data.species("charizard"), 50, {"Flamethrower"}));
    state.teams[1].push_back(buildCombatant(data.species("venusaur"),  50, {"VineWhip"}));

    BattleEngine engine(data);
    MersenneRNG rng(12345);

    int maxTurns = 30;
    while (!state.isOver() && maxTurns-- > 0) {
        engine.resolveTurn(state, UseMove{0}, UseMove{0}, rng);
    }

    REQUIRE(state.isOver());
    REQUIRE(state.sideHasLost(1));
    REQUIRE_FALSE(state.sideHasLost(0));
}
