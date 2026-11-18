#include "search/matrix_game.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <numeric>
#include <stdexcept>

using Catch::Approx;
using search::solveMatrixGame;

namespace {
// What the strategy guarantees: its worst payoff over the columns.
double guaranteed(const std::vector<std::vector<double>> &payoff,
                  const std::vector<double> &strategy) {
  double worst = 1e9;
  for (size_t j = 0; j < payoff[0].size(); ++j) {
    double expected = 0.0;
    for (size_t i = 0; i < payoff.size(); ++i)
      expected += strategy[i] * payoff[i][j];
    worst = std::min(worst, expected);
  }
  return worst;
}
} // namespace

TEST_CASE("matching pennies: each side half the time") {
  const auto solution = solveMatrixGame({{1, 0}, {0, 1}});

  CHECK(solution.strategy[0] == Approx(0.5));
  CHECK(solution.strategy[1] == Approx(0.5));
  CHECK(solution.value == Approx(0.5));
}

TEST_CASE("rock, paper, scissors: a third each, worth nothing") {
  const auto solution = solveMatrixGame({{0, -1, 1}, {1, 0, -1}, {-1, 1, 0}});

  for (double p : solution.strategy)
    CHECK(p == Approx(1.0 / 3));
  CHECK(solution.value == Approx(0.0).margin(1e-12));
}

TEST_CASE("a dominant row is played for sure") {
  const auto solution = solveMatrixGame({{0.2, 0.3}, {0.6, 0.7}, {0.1, 0.9}});

  CHECK(solution.strategy[1] == Approx(1.0));
  CHECK(solution.value == Approx(0.6));
}

TEST_CASE("a game with one column is a choice of the best row") {
  const auto solution = solveMatrixGame({{0.4}, {0.8}, {0.5}});

  CHECK(solution.strategy[1] == Approx(1.0));
  CHECK(solution.value == Approx(0.8));
}

TEST_CASE("the strategy is a distribution that guarantees the value") {
  const std::vector<std::vector<double>> payoff{
      {0.55, 0.31, 0.72, 0.40}, {0.62, 0.48, 0.20, 0.51}, {0.33, 0.67, 0.58, 0.29}};
  const auto solution = solveMatrixGame(payoff);

  CHECK(std::accumulate(solution.strategy.begin(), solution.strategy.end(), 0.0) == Approx(1.0));
  for (double p : solution.strategy)
    CHECK(p >= 0.0);
  CHECK(guaranteed(payoff, solution.strategy) == Approx(solution.value));
  // No pure row guarantees more.
  for (size_t i = 0; i < payoff.size(); ++i) {
    std::vector<double> pure(payoff.size(), 0.0);
    pure[i] = 1.0;
    CHECK(guaranteed(payoff, pure) <= solution.value + 1e-12);
  }
}

TEST_CASE("equal payoffs everywhere: any distribution, worth that payoff") {
  const auto solution = solveMatrixGame({{0.5, 0.5}, {0.5, 0.5}});

  CHECK(solution.strategy[0] + solution.strategy[1] == Approx(1.0));
  CHECK(solution.value == Approx(0.5));
}

TEST_CASE("an empty game is refused") {
  CHECK_THROWS_AS(solveMatrixGame({}), std::invalid_argument);
}
