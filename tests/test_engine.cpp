#include "helpers.hpp"

#include "engine/core/battle_state.hpp"
#include "engine/core/data_loader.hpp"
#include "engine/core/engine.hpp"
#include "engine/core/rng.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace engine;
using engine::test::buildCombatant;

TEST_CASE("Full battle ends in a KO", "[engine][integration]") {
  DataLoader data;
  engine::test::loadAll(data);

  BattleState state;
  state.teams[0][0] = buildCombatant(data, "Infernape", 100, {"Flamethrower"});
  state.teams[1][0] = buildCombatant(data, "Toxapex", 100, {"VineWhip"});
  state.team_size = {1, 1};

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
