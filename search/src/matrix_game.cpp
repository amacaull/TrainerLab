#include "search/matrix_game.hpp"

#include <algorithm>
#include <stdexcept>

namespace search {
namespace {
constexpr double kEpsilon = 1e-12;
} // namespace

MatrixSolution solveMatrixGame(const std::vector<std::vector<double>> &payoff) {
  const size_t rows = payoff.size();
  const size_t columns = rows == 0 ? 0 : payoff[0].size();
  if (rows == 0 || columns == 0)
    throw std::invalid_argument("solveMatrixGame: an empty game");

  // Shifted so that every payoff is at least 1: the game's value is then positive, and the column
  // player's problem below is bounded, with the origin feasible.
  double lowest = payoff[0][0];
  for (const auto &row : payoff)
    lowest = std::min(lowest, *std::min_element(row.begin(), row.end()));
  const double shift = 1.0 - lowest;

  // The column player's problem: maximise sum(y) subject to P y <= 1, y >= 0. Its optimum is
  // 1 / value, and the prices of its row constraints are the row player's strategy, rescaled.
  // Tableau: one row per constraint, then the objective; columns y, then the slacks, then the
  // right-hand side.
  const size_t width = columns + rows + 1;
  std::vector<std::vector<double>> t(rows + 1, std::vector<double>(width, 0.0));
  std::vector<size_t> basis(rows);
  for (size_t i = 0; i < rows; ++i) {
    for (size_t j = 0; j < columns; ++j)
      t[i][j] = payoff[i][j] + shift;
    t[i][columns + i] = 1.0;
    t[i][width - 1] = 1.0;
    basis[i] = columns + i;
  }
  for (size_t j = 0; j < columns; ++j)
    t[rows][j] = -1.0;

  while (true) {
    // Bland's rule: the first improving column enters, and the first row among ties leaves.
    size_t entering = width;
    for (size_t j = 0; j + 1 < width; ++j)
      if (t[rows][j] < -kEpsilon) {
        entering = j;
        break;
      }
    if (entering == width)
      break;
    size_t leaving = rows;
    double best = 0.0;
    for (size_t i = 0; i < rows; ++i) {
      if (t[i][entering] <= kEpsilon)
        continue;
      const double ratio = t[i][width - 1] / t[i][entering];
      if (leaving == rows || ratio < best - kEpsilon ||
          (ratio <= best + kEpsilon && basis[i] < basis[leaving]))
        leaving = i, best = ratio;
    }
    const double pivot = t[leaving][entering];
    for (double &cell : t[leaving])
      cell /= pivot;
    for (size_t i = 0; i <= rows; ++i) {
      if (i == leaving || t[i][entering] == 0.0)
        continue;
      const double factor = t[i][entering];
      for (size_t j = 0; j < width; ++j)
        t[i][j] -= factor * t[leaving][j];
    }
    basis[leaving] = entering;
  }

  const double total = t[rows][width - 1]; // 1 / (value + shift)
  MatrixSolution solution;
  solution.strategy.resize(rows);
  for (size_t i = 0; i < rows; ++i)
    solution.strategy[i] = std::max(0.0, t[rows][columns + i]) / total;
  solution.value = 1.0 / total - shift;
  return solution;
}
} // namespace search
