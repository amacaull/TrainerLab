#pragma once

#include <vector>

namespace search {
// The solution of a zero-sum game between a row player who maximises and a column player who
// minimises: the row player's mixed strategy, and the value it guarantees whatever the columns do.
struct MatrixSolution {
  std::vector<double> strategy; // one probability per row, summing to 1
  double value = 0.0;
};

// Solves the game exactly, by linear programming (the simplex method, with Bland's rule so that it
// always ends). `payoff[i][j]` is what the row player gets when it plays row i and the column
// player column j. Small games only: a battle turn has at most nine options a side.
MatrixSolution solveMatrixGame(const std::vector<std::vector<double>> &payoff);
} // namespace search
