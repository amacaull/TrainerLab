#include "search/game.hpp"

#include "engine/ffi/ffi.hpp"

#include <catch2/catch_test_macros.hpp>

namespace {
const search::Game &game() {
  static const search::Game instance(TRAINERLAB_DATA_DIR);
  return instance;
}
} // namespace

TEST_CASE("a random battle has two legal teams of six, started") {
  const engine::BattleState state = game().randomBattle(7);

  for (int side : {0, 1}) {
    CHECK(state.team_size[static_cast<size_t>(side)] == engine::kTeamSize);
    CHECK(state.active(side).revealed == 1);
    CHECK_NOTHROW(
        engine::ffi::validate_team(state.teams[static_cast<size_t>(side)], engine::kTeamSize));
  }
  CHECK_NOTHROW(engine::ffi::validate_state(state));
  CHECK(state.turn == 0);
}

TEST_CASE("the same seed gives the same battle, another seed another one") {
  CHECK(game().randomBattle(7) == game().randomBattle(7));
  CHECK_FALSE(game().randomBattle(7) == game().randomBattle(8));
}

TEST_CASE("a random playout goes to the end, and stays a valid state on the way") {
  for (uint64_t seed = 0; seed < 50; ++seed) {
    engine::BattleState state = game().randomBattle(seed);
    const search::Outcome outcome = search::playRandomly(game(), state, seed);

    CHECK(state.isOver());
    CHECK(outcome.turns == state.turn);
    CHECK(outcome.turns > 0);
    if (outcome.winner >= 0)
      CHECK(state.sideHasLost(1 - outcome.winner));
    CHECK_NOTHROW(engine::ffi::validate_state(state));
  }
}

TEST_CASE("a playout stops at the turn limit, the battle unfinished") {
  engine::BattleState state = game().randomBattle(3);
  const search::Outcome outcome = search::playRandomly(game(), state, 3, 2);

  CHECK(outcome.turns == 2);
  CHECK(outcome.winner == -1);
  CHECK_FALSE(state.isOver());
}

TEST_CASE("the same seed plays the same playout") {
  engine::BattleState a = game().randomBattle(11);
  engine::BattleState b = a;
  CHECK(search::playRandomly(game(), a, 5).turns == search::playRandomly(game(), b, 5).turns);
  CHECK(a == b);
}
