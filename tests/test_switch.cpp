#include "helpers.hpp"

#include "engine/core/battle_state.hpp"
#include "engine/core/data_loader.hpp"
#include "engine/core/engine.hpp"
#include "engine/core/rng.hpp"
#include "engine/core/switching.hpp"
#include "engine/model/status.hpp"

#include <catch2/catch_test_macros.hpp>

#include <stdexcept>
#include <variant>

using namespace engine;
using engine::test::buildCombatant;

namespace {

template <typename E> bool hasEvent(const EventLog &events) {
  for (const auto &ev : events)
    if (std::holds_alternative<E>(ev))
      return true;
  return false;
}

bool switchedInTo(const EventLog &events, int side, int teamIndex) {
  for (const auto &ev : events)
    if (auto *e = std::get_if<SwitchedInEvent>(&ev))
      if (e->who.side == side && e->who.teamIndex == teamIndex)
        return true;
  return false;
}

// Snorlax + Machamp vs Blastoise; slow, statusless matchup for switch tests.
BattleState makeTwoVsOne(const DataLoader &data) {
  BattleState state;
  state.teams[0][0] = buildCombatant(data, "Snorlax", {"BodySlam"});
  state.teams[0][1] = buildCombatant(data, "Conkeldurr", {"CloseCombat"});
  state.teams[1][0] = buildCombatant(data, "Inteleon", {"Surf"});
  state.team_size = {2, 1};
  return state;
}

} // namespace

TEST_CASE("Switch resolves before any move", "[switch][order]") {
  DataLoader data;
  engine::test::loadAll(data);
  BattleEngine engine(data);

  auto state = makeTwoVsOne(data);
  FixedRNG rng(0.5f);
  auto events = engine.resolveTurn(state, SwitchAction{1}, UseMove{0}, rng);

  // Blastoise vastly outspeeds Snorlax, yet the switch happens first.
  REQUIRE_FALSE(events.empty());
  REQUIRE(std::holds_alternative<SwitchedOutEvent>(events.front()));
  REQUIRE(switchedInTo(events, 0, 1));
  REQUIRE(state.activeIndex[0] == 1);
}

TEST_CASE("Incoming Pokemon takes the opponent's hit after a switch", "[switch]") {
  DataLoader data;
  engine::test::loadAll(data);
  BattleEngine engine(data);

  auto state = makeTwoVsOne(data);
  int machampHp = state.teams[0][1].currentHp;
  int snorlaxHp = state.teams[0][0].currentHp;
  FixedRNG rng(0.5f);
  engine.resolveTurn(state, SwitchAction{1}, UseMove{0}, rng);

  REQUIRE(state.teams[0][1].currentHp < machampHp);
  REQUIRE(state.teams[0][0].currentHp == snorlaxHp);
}

TEST_CASE("Switch-out resets stat stages", "[switch][stage]") {
  DataLoader data;
  engine::test::loadAll(data);
  BattleEngine engine(data);

  auto state = makeTwoVsOne(data);
  state.teams[0][0].stat_stages[static_cast<size_t>(StatIndex::Atk)] = 4;
  state.teams[0][0].stat_stages[static_cast<size_t>(StatIndex::Spe)] = -2;
  FixedRNG rng(0.5f);
  engine.resolveTurn(state, SwitchAction{1}, UseMove{0}, rng);

  for (int i = 0; i < kStatStageCount; ++i)
    REQUIRE(state.teams[0][0].stat_stages[static_cast<size_t>(i)] == 0);
}

TEST_CASE("Switch-out resets the Toxic counter but keeps the status", "[switch][status]") {
  DataLoader data;
  engine::test::loadAll(data);
  BattleEngine engine(data);

  auto state = makeTwoVsOne(data);
  state.teams[0][0].status = Status::Toxic;
  state.teams[0][0].status_turns = 5;
  FixedRNG rng(0.5f);
  engine.resolveTurn(state, SwitchAction{1}, UseMove{0}, rng);

  REQUIRE(state.teams[0][0].status == Status::Toxic);
  REQUIRE(state.teams[0][0].status_turns == 0);
}

TEST_CASE("Sleep turns persist across a switch", "[switch][status]") {
  DataLoader data;
  engine::test::loadAll(data);
  BattleEngine engine(data);

  auto state = makeTwoVsOne(data);
  state.teams[0][0].status = Status::Sleep;
  state.teams[0][0].status_turns = 2;
  FixedRNG rng(0.5f);
  engine.resolveTurn(state, SwitchAction{1}, UseMove{0}, rng);

  REQUIRE(state.teams[0][0].status == Status::Sleep);
  REQUIRE(state.teams[0][0].status_turns == 2);
}

TEST_CASE("Invalid switch targets are rejected before the turn runs", "[switch][validate]") {
  DataLoader data;
  engine::test::loadAll(data);
  BattleEngine engine(data);
  FixedRNG rng(0.5f);

  auto state = makeTwoVsOne(data);
  int turnBefore = state.turn;

  SECTION("out of range") {
    REQUIRE_THROWS_AS(engine.resolveTurn(state, SwitchAction{5}, UseMove{0}, rng),
                      std::invalid_argument);
  }
  SECTION("current active") {
    REQUIRE_THROWS_AS(engine.resolveTurn(state, SwitchAction{0}, UseMove{0}, rng),
                      std::invalid_argument);
  }
  SECTION("fainted teammate") {
    state.teams[0][1].currentHp = 0;
    REQUIRE_THROWS_AS(engine.resolveTurn(state, SwitchAction{1}, UseMove{0}, rng),
                      std::invalid_argument);
  }
  // Rejection happens before any mutation.
  REQUIRE(state.turn == turnBefore);
}

TEST_CASE("resolveTurn requires a replacement when the active is fainted",
          "[switch][replacement]") {
  DataLoader data;
  engine::test::loadAll(data);
  BattleEngine engine(data);
  FixedRNG rng(0.5f);

  auto state = makeTwoVsOne(data);
  state.teams[0][0].currentHp = 0;

  REQUIRE_THROWS_AS(engine.resolveTurn(state, UseMove{0}, UseMove{0}, rng), std::invalid_argument);
}

TEST_CASE("resolveReplacement performs a free switch", "[switch][replacement]") {
  DataLoader data;
  engine::test::loadAll(data);
  BattleEngine engine(data);

  auto state = makeTwoVsOne(data);
  state.teams[0][0].currentHp = 0;
  int turnBefore = state.turn;
  int blastoiseHp = state.teams[1][0].currentHp;

  auto events = engine.resolveReplacement(state, 0, 1);

  REQUIRE(switchedInTo(events, 0, 1));
  REQUIRE(state.activeIndex[0] == 1);
  REQUIRE(state.turn == turnBefore);
  REQUIRE(state.teams[1][0].currentHp == blastoiseHp);
}

TEST_CASE("resolveReplacement rejects a non-fainted active or invalid target",
          "[switch][replacement]") {
  DataLoader data;
  engine::test::loadAll(data);
  BattleEngine engine(data);

  auto state = makeTwoVsOne(data);
  REQUIRE_THROWS_AS(engine.resolveReplacement(state, 0, 1), std::invalid_argument);

  state.teams[0][0].currentHp = 0;
  REQUIRE_THROWS_AS(engine.resolveReplacement(state, 0, 0), std::invalid_argument);
  REQUIRE_THROWS_AS(engine.resolveReplacement(state, 0, 2), std::invalid_argument);
}

TEST_CASE("UTurn deals damage then switches the user out", "[switch][pivot]") {
  DataLoader data;
  engine::test::loadAll(data);
  BattleEngine engine(data);

  BattleState state;
  state.teams[0][0] = buildCombatant(data, "Gyarados", {"UTurn"});
  state.teams[0][1] = buildCombatant(data, "Snorlax", {"BodySlam"});
  state.teams[1][0] = buildCombatant(data, "Inteleon", {"Surf"});
  state.team_size = {2, 1};

  int blastoiseHp = state.teams[1][0].currentHp;
  FixedRNG rng(0.5f);
  auto events = engine.resolveTurn(state, UseMove{0, 1}, UseMove{0}, rng);

  REQUIRE(state.teams[1][0].currentHp < blastoiseHp); // damage landed first
  REQUIRE(switchedInTo(events, 0, 1));
  REQUIRE(state.activeIndex[0] == 1);
}

TEST_CASE("Opponent's slower move hits the Pokemon brought in by the pivot", "[switch][pivot]") {
  DataLoader data;
  engine::test::loadAll(data);
  BattleEngine engine(data);

  BattleState state;
  // Gyarados (81) outspeeds Snorlax's side? Opponent: Snorlax (30), slower.
  state.teams[0][0] = buildCombatant(data, "Gyarados", {"UTurn"});
  state.teams[0][1] = buildCombatant(data, "Conkeldurr", {"CloseCombat"});
  state.teams[1][0] = buildCombatant(data, "Snorlax", {"BodySlam"});
  state.team_size = {2, 1};

  int gyaradosHp = state.teams[0][0].currentHp;
  int machampHp = state.teams[0][1].currentHp;
  FixedRNG rng(0.5f);
  engine.resolveTurn(state, UseMove{0, 1}, UseMove{0}, rng);

  REQUIRE(state.teams[0][0].currentHp == gyaradosHp); // pivoted out untouched
  REQUIRE(state.teams[0][1].currentHp < machampHp);   // incoming took BodySlam
}

TEST_CASE("Pivot with an empty bench is damage-only", "[switch][pivot]") {
  DataLoader data;
  engine::test::loadAll(data);
  BattleEngine engine(data);

  BattleState state;
  state.teams[0][0] = buildCombatant(data, "Gyarados", {"UTurn"});
  state.teams[1][0] = buildCombatant(data, "Snorlax", {"BodySlam"});
  state.team_size = {1, 1};

  int snorlaxHp = state.teams[1][0].currentHp;
  FixedRNG rng(0.5f);
  auto events = engine.resolveTurn(state, UseMove{0}, UseMove{0}, rng);

  REQUIRE(state.teams[1][0].currentHp < snorlaxHp);
  REQUIRE_FALSE(hasEvent<SwitchedOutEvent>(events));
  REQUIRE(state.activeIndex[0] == 0);
}

TEST_CASE("Pivot with pivotTarget -1 auto-picks the first healthy teammate", "[switch][pivot]") {
  DataLoader data;
  engine::test::loadAll(data);
  BattleEngine engine(data);

  BattleState state;
  state.teams[0][0] = buildCombatant(data, "Luxray", {"VoltSwitch"});
  state.teams[0][1] = buildCombatant(data, "Snorlax", {"BodySlam"});
  state.teams[0][2] = buildCombatant(data, "Conkeldurr", {"CloseCombat"});
  state.teams[1][0] = buildCombatant(data, "Inteleon", {"Surf"});
  state.team_size = {3, 1};
  state.teams[0][1].currentHp = 0; // slot 1 fainted: auto must pick slot 2

  FixedRNG rng(0.5f);
  auto events = engine.resolveTurn(state, UseMove{0}, UseMove{0}, rng);

  REQUIRE(switchedInTo(events, 0, 2));
  REQUIRE(state.activeIndex[0] == 2);
}
