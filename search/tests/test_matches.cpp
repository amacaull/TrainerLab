#include "search/self_play.hpp"

#include "random_network.hpp"

#include "engine/core/observation.hpp"
#include "engine/core/rng.hpp"
#include "engine/core/switching.hpp"

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
const search::Game &game() {
  static const search::Game instance(TRAINERLAB_DATA_DIR);
  return instance;
}

// The teams of random battles cut to `size`, each with its seed: a measure's battles.
std::vector<search::Match> matches(int count, int size, uint64_t firstSeed) {
  std::vector<search::Match> out;
  for (int k = 0; k < count; ++k) {
    const engine::BattleState state = game().randomBattle(static_cast<uint64_t>(100 + k));
    search::Match match;
    for (size_t side = 0; side < 2; ++side)
      for (int i = 0; i < size; ++i)
        match.teams[side].push_back(state.teams[side][static_cast<size_t>(i)].species_id);
    match.seed = firstSeed + static_cast<uint64_t>(k);
    out.push_back(match);
  }
  return out;
}

search::MatchSettings small(int lanes = 16, int replacements = 4) {
  search::MatchSettings settings;
  settings.planner.simulations = 8;
  settings.planner.worlds = 2;
  settings.replacementSimulations = replacements;
  settings.lanes = lanes;
  settings.maxTurns = 60;
  return settings;
}

// One battle as Python's referee plays it (ai/src/trainerlab/game/environment.py), each side
// rule 11, one decision at a time: the dice of turn t come from seed * 1'000'003 + t, the faster
// side replaces first, and the searches take the seeds seed * 1'000'003 + 0, 1, 2…
search::Outcome oneByOne(const search::Network &net, const search::MatchSettings &settings,
                         engine::BattleState state, uint64_t seed) {
  const engine::BattleEngine &rules = game().rules();
  const search::Planner planner(game(), net, settings.planner);
  uint64_t decision = 0;
  auto dice = [&] {
    return engine::MersenneRNG(seed * 1'000'003 + static_cast<uint64_t>(state.turn));
  };
  while (!state.isOver() && state.turn < settings.maxTurns) {
    if (state.active(0).isFainted() || state.active(1).isFainted()) {
      engine::MersenneRNG order = dice();
      const int first = rules.fasterSide(state, order);
      const int side = state.active(first).isFainted() ? first : 1 - first;
      const engine::BattleState view = engine::observe(state, side);
      const std::vector<int> options = engine::validSwitchTargets(view, side);
      int chosen = 0;
      if (settings.replacementSimulations > 0 && options.size() > 1) {
        chosen = planner.searchReplacement(view, side, options, seed * 1'000'003 + decision++,
                                           settings.replacementSimulations);
      } else {
        const auto mask = search::Planner::replacementMask(view, side);
        chosen = search::Planner::pickReplacement(net.evaluate(view, side, mask.data()));
      }
      rules.resolveReplacement(state, side, chosen);
      continue;
    }
    std::array<engine::Action, 2> chosen{engine::UseMove{0}, engine::UseMove{0}};
    for (int side : {0, 1}) {
      const std::vector<engine::Action> legal = rules.legalActions(state, side);
      const int index = legal.size() == 1
                            ? 0
                            : planner.chooseAction(engine::observe(state, side), side, legal,
                                                   seed * 1'000'003 + decision++);
      chosen[static_cast<size_t>(side)] = legal[static_cast<size_t>(index)];
    }
    engine::MersenneRNG turn = dice();
    rules.resolveTurn(state, chosen[0], chosen[1], turn);
  }
  const bool lost0 = state.sideHasLost(0), lost1 = state.sideHasLost(1);
  return {lost0 == lost1 ? -1 : (lost1 ? 0 : 1), state.turn};
}
} // namespace

TEST_CASE("a measure's battles, side by side, play as rule 11 does one decision at a time") {
  // Bit for bit with our kernel; BLAS sums a batch in its own order (network.hpp).
  if (std::string(search::Network::backend()) != "kernel") {
    WARN("not checked: the layers go through BLAS, which sums a batch in its own order");
    return;
  }
  const search::Network net = search::testing::randomNetwork(game());
  const std::vector<search::Match> battles = matches(6, 6, 11);

  const search::MatchResult together = search::playMatches(game(), net, small(4), battles, 2);

  for (size_t k = 0; k < battles.size(); ++k) {
    const search::Outcome alone =
        oneByOne(net, small(), search::matchStart(battles[k]), battles[k].seed);
    CHECK(together.winner[k] == alone.winner);
    CHECK(together.turns[k] == alone.turns);
  }
}

TEST_CASE("without searched replacements, the network's favourite is sent in") {
  if (std::string(search::Network::backend()) != "kernel")
    return;
  const search::Network net = search::testing::randomNetwork(game());
  const std::vector<search::Match> battles = matches(4, 3, 21);

  const search::MatchResult played = search::playMatches(game(), net, small(16, 0), battles, 1);

  for (size_t k = 0; k < battles.size(); ++k)
    CHECK(played.turns[k] ==
          oneByOne(net, small(16, 0), search::matchStart(battles[k]), battles[k].seed).turns);
}

TEST_CASE("a measure's battles are the same for the same threads, and counted as they end") {
  const search::Network net = search::testing::randomNetwork(game());
  const std::vector<search::Match> battles = matches(7, 2, 1);
  std::atomic<int> done{0};

  const search::MatchResult one = search::playMatches(game(), net, small(2), battles, 3, &done);
  const search::MatchResult again = search::playMatches(game(), net, small(2), battles, 3);

  CHECK(one.winner == again.winner);
  CHECK(one.turns == again.turns);
  CHECK(one.winner.size() == 7);
  CHECK(done == 7);
}

TEST_CASE("with our kernel, threads and lanes change nothing: a battle ending lets the next in") {
  if (std::string(search::Network::backend()) != "kernel")
    return;
  const search::Network net = search::testing::randomNetwork(game());
  const std::vector<search::Match> battles = matches(9, 2, 30);

  const search::MatchResult wide = search::playMatches(game(), net, small(16), battles, 1);
  const search::MatchResult narrow = search::playMatches(game(), net, small(2), battles, 3);

  CHECK(wide.winner == narrow.winner);
  CHECK(wide.turns == narrow.turns);
}

TEST_CASE("a measure asked to stop stops") {
  const search::Network net = search::testing::randomNetwork(game());
  const std::atomic<bool> stop{true};
  CHECK_THROWS_AS(search::playMatches(game(), net, small(), matches(3, 2, 1), 1, nullptr, &stop),
                  search::Stopped);
}

TEST_CASE("a measure refuses what it cannot play") {
  const search::Network net = search::testing::randomNetwork(game());
  const std::vector<search::Match> battles = matches(1, 2, 1);
  CHECK_THROWS_AS(search::playMatches(game(), net, small(), battles, 0), std::invalid_argument);
  CHECK_THROWS_AS(search::playMatches(game(), net, small(0), battles, 1), std::invalid_argument);
  CHECK_THROWS_AS(search::playMatches(game(), net, small(16, 1), battles, 1),
                  std::invalid_argument);
  search::Match empty;
  empty.teams[0] = {0};
  CHECK_THROWS_AS(search::playMatches(game(), net, small(), {empty}, 1), std::invalid_argument);
}
