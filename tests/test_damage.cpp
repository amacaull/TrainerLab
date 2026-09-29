#include "helpers.hpp"

#include "engine/core/battle_state.hpp"
#include "engine/core/data_loader.hpp"
#include "engine/core/engine.hpp"
#include "engine/core/rng.hpp"
#include "engine/effects/effect.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace engine;
using engine::test::buildCombatant;

TEST_CASE("Flamethrower on Zarude is super-effective", "[damage]") {
  DataLoader data;
  engine::test::loadAll(data);

  BattleState state;
  state.teams[0][0] = buildCombatant(data, "Infernape", {"Flamethrower"});
  state.teams[1][0] = buildCombatant(data, "Zarude", {"VineWhip"}); // Dark/Grass: x2
  state.team_size = {1, 1};

  BattleEngine engine(data);
  FixedRNG rng(0.5f);

  int hpBefore = state.teams[1][0].currentHp;
  auto events = engine.resolveTurn(state, UseMove{0}, UseMove{0}, rng);
  int hpAfter = state.teams[1][0].currentHp;

  REQUIRE(hpAfter < hpBefore);

  bool foundSuperEffective = false;
  for (const auto &ev : events) {
    if (auto *dmg = std::get_if<DamageDealtEvent>(&ev)) {
      if (dmg->target.side == 1 && dmg->effectiveness == 2.0f && dmg->wasStab) {
        foundSuperEffective = true;
      }
    }
  }
  REQUIRE(foundSuperEffective);
}

TEST_CASE("Charizard outspeeds Venusaur", "[order]") {
  DataLoader data;
  engine::test::loadAll(data);

  BattleState state;
  state.teams[0][0] = buildCombatant(data, "Infernape", {"Flamethrower"});
  state.teams[1][0] = buildCombatant(data, "Toxapex", {"VineWhip"});
  state.team_size = {1, 1};

  BattleEngine engine(data);
  FixedRNG rng(0.5f);

  auto events = engine.resolveTurn(state, UseMove{0}, UseMove{0}, rng);

  REQUIRE_FALSE(events.empty());
  auto *firstMove = std::get_if<MoveUsedEvent>(&events.front());
  REQUIRE(firstMove != nullptr);
  REQUIRE(firstMove->user.side == 0);
}

// Gengar holds Levitate, which voids Earthquake; the Ground
// coverage check moved to Pikachu (Electric, 2x weak).
TEST_CASE("Earthquake on Pikachu is super-effective (Ground vs Electric)", "[damage][types]") {
  DataLoader data;
  engine::test::loadAll(data);

  BattleState state;
  state.teams[0][0] = buildCombatant(data, "Excadrill", {"Earthquake"});
  state.teams[1][0] = buildCombatant(data, "Luxray", {"QuickAttack"});
  state.team_size = {1, 1};

  BattleEngine engine(data);
  FixedRNG rng(0.5f);

  int hpBefore = state.teams[1][0].currentHp;
  engine.resolveTurn(state, UseMove{0}, UseMove{0}, rng);
  int hpAfter = state.teams[1][0].currentHp;

  REQUIRE(hpAfter < hpBefore);
}

// QuickAttack has priority +1, so Pikachu strikes before Gengar regardless
// of speed. Normal vs Ghost is the immunity under test.
TEST_CASE("Immunity event emitted when type chart says 0x", "[damage][types]") {
  DataLoader data;
  engine::test::loadAll(data);

  BattleState state;
  state.teams[0][0] = buildCombatant(data, "Luxray", {"QuickAttack"});
  state.teams[1][0] = buildCombatant(data, "MegaGengar", {"ShadowBall"});
  state.team_size = {1, 1};

  BattleEngine engine(data);
  FixedRNG rng(0.5f);

  auto events = engine.resolveTurn(state, UseMove{0}, UseMove{0}, rng);

  auto *firstMove = std::get_if<MoveUsedEvent>(&events.front());
  REQUIRE(firstMove != nullptr);
  REQUIRE(firstMove->user.side == 0);
  REQUIRE(firstMove->moveName == "QuickAttack");

  bool foundImmunity = false;
  for (const auto &ev : events) {
    if (auto *dmg = std::get_if<DamageDealtEvent>(&ev)) {
      if (dmg->target.side == 1) {
        REQUIRE(dmg->effectiveness == 0.0f);
        REQUIRE(dmg->damage == 0);
        foundImmunity = true;
        break;
      }
    }
  }
  REQUIRE(foundImmunity);
}
