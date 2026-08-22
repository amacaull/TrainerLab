#include "helpers.hpp"

#include "engine/core/battle_state.hpp"
#include "engine/core/data_loader.hpp"
#include "engine/core/validate.hpp"
#include "engine/model/status.hpp"

#include <catch2/catch_test_macros.hpp>

#include <stdexcept>
#include <string>

using namespace engine;
using engine::test::buildCombatant;

namespace {

BattleState makeValidState(const DataLoader &data) {
  BattleState state;
  state.teams[0][0] = buildCombatant(data, "Infernape", {"Flamethrower"});
  state.teams[1][0] = buildCombatant(data, "Toxapex", {"VineWhip"});
  state.team_size = {1, 1};
  state.activeIndex = {0, 0};
  return state;
}

} // namespace

TEST_CASE("validateState accepts a valid state", "[validate]") {
  DataLoader data;
  engine::test::loadAll(data);
  REQUIRE_NOTHROW(validateState(makeValidState(data), data));
}

TEST_CASE("validateState rejects empty team_size", "[validate]") {
  DataLoader data;
  engine::test::loadAll(data);
  auto s = makeValidState(data);
  s.team_size[0] = 0;
  REQUIRE_THROWS_AS(validateState(s, data), std::invalid_argument);
}

TEST_CASE("validateState rejects team_size > kTeamSize", "[validate]") {
  DataLoader data;
  engine::test::loadAll(data);
  auto s = makeValidState(data);
  s.team_size[0] = kTeamSize + 1;
  REQUIRE_THROWS_AS(validateState(s, data), std::invalid_argument);
}

TEST_CASE("validateState rejects activeIndex out of bounds", "[validate]") {
  DataLoader data;
  engine::test::loadAll(data);
  auto s = makeValidState(data);
  s.activeIndex[0] = 5;
  REQUIRE_THROWS_AS(validateState(s, data), std::invalid_argument);
}

TEST_CASE("validateState rejects invalid species_id", "[validate]") {
  DataLoader data;
  engine::test::loadAll(data);
  auto s = makeValidState(data);
  s.teams[0][0].species_id = 9999;
  REQUIRE_THROWS_AS(validateState(s, data), std::invalid_argument);
}

TEST_CASE("validateState rejects level < 1", "[validate]") {
  DataLoader data;
  engine::test::loadAll(data);
  auto s = makeValidState(data);
  s.teams[0][0].level = 0;
  REQUIRE_THROWS_AS(validateState(s, data), std::invalid_argument);
}

TEST_CASE("validateState rejects level > 100", "[validate]") {
  DataLoader data;
  engine::test::loadAll(data);
  auto s = makeValidState(data);
  s.teams[0][0].level = 101;
  REQUIRE_THROWS_AS(validateState(s, data), std::invalid_argument);
}

TEST_CASE("validateState rejects negative currentHp", "[validate]") {
  DataLoader data;
  engine::test::loadAll(data);
  auto s = makeValidState(data);
  s.teams[0][0].currentHp = -5;
  REQUIRE_THROWS_AS(validateState(s, data), std::invalid_argument);
}

TEST_CASE("validateState rejects currentHp > stats.hp", "[validate]") {
  DataLoader data;
  engine::test::loadAll(data);
  auto s = makeValidState(data);
  s.teams[0][0].currentHp = s.teams[0][0].stats.hp + 1;
  REQUIRE_THROWS_AS(validateState(s, data), std::invalid_argument);
}

TEST_CASE("validateState accepts currentHp == 0 (fainted)", "[validate]") {
  DataLoader data;
  engine::test::loadAll(data);
  auto s = makeValidState(data);
  s.teams[0][0].currentHp = 0;
  REQUIRE_NOTHROW(validateState(s, data));
}

TEST_CASE("validateState rejects invalid move_id", "[validate]") {
  DataLoader data;
  engine::test::loadAll(data);
  auto s = makeValidState(data);
  s.teams[0][0].move_ids[0] = 9999;
  REQUIRE_THROWS_AS(validateState(s, data), std::invalid_argument);
}

TEST_CASE("validateState rejects invalid status value", "[validate]") {
  DataLoader data;
  engine::test::loadAll(data);
  auto s = makeValidState(data);
  s.teams[0][0].status = static_cast<Status>(99);
  REQUIRE_THROWS_AS(validateState(s, data), std::invalid_argument);
}

TEST_CASE("validateState rejects negative status_turns", "[validate]") {
  DataLoader data;
  engine::test::loadAll(data);
  auto s = makeValidState(data);
  s.teams[0][0].status = Status::Toxic;
  s.teams[0][0].status_turns = -1;
  REQUIRE_THROWS_AS(validateState(s, data), std::invalid_argument);
}

TEST_CASE("validateState accepts kNoMove sentinel in move slots", "[validate]") {
  DataLoader data;
  engine::test::loadAll(data);
  auto s = makeValidState(data);
  s.teams[0][0].move_ids[1] = kNoMove;
  s.teams[0][0].move_ids[2] = kNoMove;
  s.teams[0][0].move_ids[3] = kNoMove;
  REQUIRE_NOTHROW(validateState(s, data));
}

TEST_CASE("validateState error message is descriptive", "[validate]") {
  DataLoader data;
  engine::test::loadAll(data);
  auto s = makeValidState(data);
  s.teams[0][0].level = 200;

  try {
    validateState(s, data);
    FAIL("Expected validateState to throw");
  } catch (const std::invalid_argument &e) {
    std::string msg = e.what();
    REQUIRE(msg.find("level") != std::string::npos);
    REQUIRE(msg.find("200") != std::string::npos);
    REQUIRE(msg.find("side 0") != std::string::npos);
  }
}
