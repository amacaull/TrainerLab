#include "engine/battle_state.hpp"
#include "engine/data_loader.hpp"
#include "engine/effects/effect.hpp"
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

TEST_CASE("Flamethrower on Venusaur is super-effective", "[damage]") {
    DataLoader data;
    data.loadAll(BATTLE_ENGINE_DATA_DIR);

    BattleState state;
    state.teams[0].push_back(buildCombatant(data.species("charizard"), 50, {"Flamethrower"}));
    state.teams[1].push_back(buildCombatant(data.species("venusaur"),  50, {"VineWhip"}));

    BattleEngine engine(data);
    FixedRNG rng(0.5f); // rangeInt returns min => 85% roll

    int hpBefore = state.teams[1][0].currentHp;
    auto events = engine.resolveTurn(state, UseMove{0}, UseMove{0}, rng);
    int hpAfter = state.teams[1][0].currentHp;
    int damage = hpBefore - hpAfter;

    REQUIRE(damage > 0);

    bool foundSuperEffective = false;
    for (const auto& ev : events) {
        if (auto* dmg = std::get_if<DamageDealtEvent>(&ev)) {
            if (dmg->target.side == 1 && dmg->effectiveness == 2.0f && dmg->wasStab) {
                foundSuperEffective = true;
            }
        }
    }
    REQUIRE(foundSuperEffective);
}

TEST_CASE("Charizard outspeeds Venusaur", "[order]") {
    DataLoader data;
    data.loadAll(BATTLE_ENGINE_DATA_DIR);

    BattleState state;
    state.teams[0].push_back(buildCombatant(data.species("charizard"), 50, {"Flamethrower"}));
    state.teams[1].push_back(buildCombatant(data.species("venusaur"),  50, {"VineWhip"}));

    BattleEngine engine(data);
    FixedRNG rng(0.5f);

    auto events = engine.resolveTurn(state, UseMove{0}, UseMove{0}, rng);

    REQUIRE_FALSE(events.empty());
    auto* firstMove = std::get_if<MoveUsedEvent>(&events.front());
    REQUIRE(firstMove != nullptr);
    REQUIRE(firstMove->user.side == 0);
}
