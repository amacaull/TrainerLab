#include "search/forecaster.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <vector>

TEST_CASE("a model with no hidden layer is a logistic regression") {
  // Two features, standardised, then 2 x z0 - z1 + 0.5.
  const search::WinModel model{{{2.0, -1.0}}, {{0.5}}, {1.0, 0.0}, {2.0, 1.0}};
  const float row[] = {3.0f, 1.0f}; // z = (1, 1)

  CHECK(model.predict(row) == Catch::Approx(1.0 / (1.0 + std::exp(-1.5))));
}

TEST_CASE("a hidden layer of ReLU units feeds the sigmoid") {
  // One feature, two hidden units (relu(z) and relu(-z)), the output their difference: z itself.
  const search::WinModel model{{{1.0, -1.0}, {1.0, -1.0}}, {{0.0, 0.0}, {0.0}}, {0.0}, {1.0}};
  const float positive[] = {2.0f};
  const float negative[] = {-2.0f};

  CHECK(model.predict(positive) == Catch::Approx(1.0 / (1.0 + std::exp(-2.0))));
  CHECK(model.predict(negative) == Catch::Approx(1.0 / (1.0 + std::exp(2.0))));
}

TEST_CASE("an extreme score is clipped, not overflowed") {
  const search::WinModel model{{{1000.0}}, {{0.0}}, {0.0}, {1.0}};
  const float row[] = {-1.0f};

  CHECK(model.predict(row) > 0.0);
}
