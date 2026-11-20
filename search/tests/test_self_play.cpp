#include "search/self_play.hpp"

#include "random_network.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <set>
#include <stdexcept>
#include <string>

namespace {
const search::Game &game() {
  static const search::Game instance(TRAINERLAB_DATA_DIR);
  return instance;
}

// Every search full (16 simulations) unless `fast` is given: then a quarter of them, the others
// `fast` simulations.
search::SelfPlaySettings small(int maxTurns = 40, int lanes = 16, int fast = 0) {
  return {{16, 2, 1.5, 0.25}, maxTurns, lanes, fast, 0.25};
}
} // namespace

TEST_CASE("self-play records a policy over the legal options and each side's result") {
  const search::Network net = search::testing::randomNetwork(game());
  const search::SelfPlayData data = search::selfPlay(game(), net, small(), 3, 7, 1);
  const size_t rows = static_cast<size_t>(data.rows());
  constexpr size_t actions = search::Encoding::kActions;

  REQUIRE(rows > 0);
  CHECK(data.policy.size() == rows * actions);
  CHECK(data.mask.size() == rows * actions);
  CHECK(data.game.size() == rows);
  CHECK(data.turns.size() == 3);
  CHECK(data.winner.size() == 3);
  for (size_t r = 0; r < rows; ++r) {
    float total = 0.0f;
    int legal = 0;
    for (size_t a = 0; a < actions; ++a) {
      const float p = data.policy[r * actions + a];
      CHECK((data.mask[r * actions + a] == 1.0f || p == 0.0f)); // nothing on an illegal option
      total += p;
      legal += data.mask[r * actions + a] == 1.0f;
    }
    CHECK(std::abs(total - 1.0f) < 1e-5f);
    CHECK(legal > 1); // forced choices are not recorded
    const int winner = data.winner[static_cast<size_t>(data.game[r])];
    CHECK((winner < 0 ? data.result[r] == 0.0f : std::abs(data.result[r]) == 1.0f));
  }
}

TEST_CASE("self-play's rows are the same for any number of threads") {
  const search::Network net = search::testing::randomNetwork(game());
  const search::SelfPlayData one = search::selfPlay(game(), net, small(40, 2, 4), 4, 3, 1);
  const search::SelfPlayData three = search::selfPlay(game(), net, small(40, 2, 4), 4, 3, 3);

  CHECK(one.policy == three.policy);
  CHECK(one.pokemon == three.pokemon);
  CHECK(one.result == three.result);
  CHECK(one.game == three.game);
  CHECK(std::set<int>(one.game.begin(), one.game.end()).size() == 4);
}

TEST_CASE("battles played side by side, their queries batched, play as they do one by one") {
  // Bit for bit with our kernel; BLAS sums a batch in its own order (network.hpp).
  if (std::string(search::Network::backend()) != "kernel")
    SKIP("the layers go through BLAS");
  const search::Network net = search::testing::randomNetwork(game());
  const search::SelfPlayData alone = search::selfPlay(game(), net, small(40, 1), 5, 11, 2);
  const search::SelfPlayData together = search::selfPlay(game(), net, small(40, 4), 5, 11, 2);

  CHECK(alone.policy == together.policy);
  CHECK(alone.pokemon == together.pokemon);
  CHECK(alone.result == together.result);
  CHECK(alone.turns == together.turns);
}

TEST_CASE("with fast searches, only the full ones are recorded: a share of the decisions") {
  const search::Network net = search::testing::randomNetwork(game());
  search::SelfPlaySettings none = small(40, 16, 4), all = small(40, 16, 4);
  none.fullShare = 0.0;
  all.fullShare = 1.0;
  const search::SelfPlayData played = search::selfPlay(game(), net, none, 2, 5, 1);
  CHECK(played.rows() == 0); // the battles are played, nothing recorded
  CHECK(played.turns.size() == 2);
  CHECK(played.turns[0] > 0);

  // All full: as without fast searches.
  const search::SelfPlayData full = search::selfPlay(game(), net, all, 2, 5, 1);
  const search::SelfPlayData plain = search::selfPlay(game(), net, small(), 2, 5, 1);
  CHECK(full.policy == plain.policy);
  CHECK(full.turns == plain.turns);

  const search::SelfPlayData mixed = search::selfPlay(game(), net, small(40, 16, 4), 6, 5, 1);
  int searched = 0;
  for (int turns : mixed.turns)
    searched += turns;
  CHECK(mixed.rows() > 0);
  CHECK(mixed.rows() < searched); // about a quarter of the decisions searched
}

TEST_CASE("a battle stopped at the turn limit is a draw: every result 0") {
  const search::Network net = search::testing::randomNetwork(game());
  const search::SelfPlayData data = search::selfPlay(game(), net, small(2), 2, 1, 2);

  for (int8_t winner : data.winner)
    CHECK(winner == -1);
  for (float result : data.result)
    CHECK(result == 0.0f);
}

TEST_CASE("self-play refuses settings it cannot play with") {
  const search::Network net = search::testing::randomNetwork(game());
  search::SelfPlaySettings one = small();
  one.planner.simulations = 1;
  CHECK_THROWS_AS(search::selfPlay(game(), net, one, 1, 0, 1), std::invalid_argument);
  CHECK_THROWS_AS(search::selfPlay(game(), net, small(), 1, 0, 0), std::invalid_argument);
  CHECK_THROWS_AS(search::selfPlay(game(), net, small(40, 0), 1, 0, 1), std::invalid_argument);
  CHECK_THROWS_AS(search::selfPlay(game(), net, small(40, 16, 1), 1, 0, 1), std::invalid_argument);
  search::SelfPlaySettings share = small(40, 16, 4);
  share.fullShare = 1.5;
  CHECK_THROWS_AS(search::selfPlay(game(), net, share, 1, 0, 1), std::invalid_argument);
}
