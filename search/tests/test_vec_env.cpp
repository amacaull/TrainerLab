#include "search/vec_env.hpp"

#include "search/encoding.hpp"

#include <catch2/catch_test_macros.hpp>

#include <random>
#include <stdexcept>
#include <vector>

namespace {
const search::Game &game() {
  static const search::Game instance(TRAINERLAB_DATA_DIR);
  return instance;
}

struct Arrays {
  std::vector<float> pokemon, field, mask;
  std::vector<int32_t> ids;
  explicit Arrays(int count) {
    const auto &e = game().encoding();
    const auto sides = static_cast<size_t>(2 * count);
    pokemon.resize(sides * search::Encoding::kSlots * e.pokemonNames().size());
    ids.resize(sides * search::Encoding::kSlots * search::Encoding::kIds);
    field.resize(sides * e.fieldNames().size());
    mask.resize(sides * search::Encoding::kActions);
  }
};

// A legal option for each side, at random; 0 where a side has none.
std::vector<int32_t> randomActions(const std::vector<float> &mask, std::mt19937 &rng) {
  std::vector<int32_t> actions(mask.size() / search::Encoding::kActions, 0);
  for (size_t k = 0; k < actions.size(); ++k) {
    std::vector<int32_t> legal;
    for (int a = 0; a < search::Encoding::kActions; ++a)
      if (mask[k * search::Encoding::kActions + static_cast<size_t>(a)] == 1.0f)
        legal.push_back(a);
    if (!legal.empty())
      actions[k] = legal[std::uniform_int_distribution<size_t>(0, legal.size() - 1)(rng)];
  }
  return actions;
}
} // namespace

TEST_CASE("random play finishes battles, zero-sum, and every battle starts again") {
  search::VecEnv env(game(), 8, 1);
  Arrays a(8);
  std::vector<float> rewards(16);
  std::vector<uint8_t> done(8);
  std::mt19937 rng(3);
  int ended = 0;
  for (int step = 0; step < 400; ++step) {
    env.observe(a.pokemon.data(), a.ids.data(), a.field.data(), a.mask.data());
    for (int b = 0; b < 8; ++b) // someone always has a decision to make
      CHECK(std::count(a.mask.begin() + 20 * b, a.mask.begin() + 20 * b + 20, 1.0f) > 0);
    env.step(randomActions(a.mask, rng).data(), rewards.data(), done.data());
    for (int b = 0; b < 8; ++b) {
      CHECK(rewards[2 * b] == -rewards[2 * b + 1]);
      if (!done[b])
        CHECK(rewards[2 * b] == 0.0f);
      ended += done[b];
    }
  }
  CHECK(ended > 0);
  CHECK(env.finished() == ended);
}

TEST_CASE("the same seed and the same actions give the same battles") {
  auto run = [](uint64_t seed) {
    search::VecEnv env(game(), 4, seed);
    Arrays a(4);
    std::vector<float> rewards(8), all;
    std::vector<uint8_t> done(4);
    std::mt19937 rng(5);
    for (int step = 0; step < 100; ++step) {
      env.observe(a.pokemon.data(), a.ids.data(), a.field.data(), a.mask.data());
      env.step(randomActions(a.mask, rng).data(), rewards.data(), done.data());
      all.insert(all.end(), rewards.begin(), rewards.end());
    }
    all.insert(all.end(), a.pokemon.begin(), a.pokemon.end());
    return all;
  };
  CHECK(run(1) == run(1));
  CHECK_FALSE(run(1) == run(2));
}

TEST_CASE("an illegal option is refused before any battle moves") {
  search::VecEnv env(game(), 2, 1);
  Arrays before(2), after(2);
  env.observe(before.pokemon.data(), before.ids.data(), before.field.data(), before.mask.data());
  std::vector<int32_t> actions{0, 0, 0, 42};
  std::vector<float> rewards(4);
  std::vector<uint8_t> done(2);

  CHECK_THROWS_AS(env.step(actions.data(), rewards.data(), done.data()), std::invalid_argument);
  env.observe(after.pokemon.data(), after.ids.data(), after.field.data(), after.mask.data());
  CHECK(before.pokemon == after.pokemon);
}
