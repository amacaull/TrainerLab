// How fast the engine plays, through each of its two APIs: the reference that the C++ search of
// palier 5 is measured against.
//
//   search_bench            # about 2 s per line
//   search_bench 0.2        # seconds per line
//
// Every line plays from the same 64 positions as `trainerlab bench` (search::openings): random
// battles, 3 random turns in. A line reports how many times per second it does its one thing.

#include "search/game.hpp"

#include "engine/core/observation.hpp"
#include "engine/ffi/ffi.hpp"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <random>
#include <vector>

namespace {
using Clock = std::chrono::steady_clock;

constexpr int kPositions = 64; // trainerlab bench plays the same ones

// Calls `step` (which returns how many units it did) until `seconds` have passed; units per second.
double rate(double seconds, const std::function<long()> &step) {
  long units = 0;
  const auto start = Clock::now();
  std::chrono::duration<double> elapsed{};
  do {
    units += step();
    elapsed = Clock::now() - start;
  } while (elapsed.count() < seconds);
  return static_cast<double>(units) / elapsed.count();
}

void line(const char *what, const char *unit, double perSecond) {
  std::printf("%-44s %12.0f %s/s\n", what, perSecond, unit);
}

// A random playout through the flat API, as Python plays it: each call validates and copies.
long flatPlayout(engine::BattleState state, uint64_t seed) {
  namespace ffi = engine::ffi;
  std::mt19937_64 choices(seed);
  auto pick = [&choices](const auto &options) {
    return options[std::uniform_int_distribution<size_t>(0, options.size() - 1)(choices)];
  };
  const int start = state.turn;
  while (!ffi::is_over(state) && state.turn < 1000) {
    const uint64_t turnSeed = seed * 1'000'003 + static_cast<uint64_t>(state.turn);
    const int first = ffi::faster_side(state, turnSeed);
    bool replaced = false;
    for (int side : {first, 1 - first}) {
      const std::vector<int> options = ffi::legal_replacements(state, side);
      if (options.empty())
        continue;
      ffi::resolve_replacement(state, side, pick(options));
      replaced = true;
      break;
    }
    if (!replaced)
      ffi::resolve_turn(state, pick(ffi::legal_actions(state, 0)),
                        pick(ffi::legal_actions(state, 1)), turnSeed);
  }
  return state.turn - start;
}
} // namespace

int main(int argc, char **argv) {
  const double seconds = argc > 1 ? std::atof(argv[1]) : 2.0;
  const search::Game game(TRAINERLAB_DATA_DIR);
  const engine::BattleEngine &rules = game.rules();

  const std::vector<engine::BattleState> positions = search::openings(game, kPositions);
  std::printf("%zu positions, %.1f s per line\n\n", positions.size(), seconds);

  size_t next = 0;
  auto position = [&]() -> const engine::BattleState & {
    next = (next + 1) % positions.size();
    return positions[next];
  };
  uint64_t seed = 0;

  line("random playout, flat API (as from Python)", "turns",
       rate(seconds, [&] { return flatPlayout(position(), ++seed); }));
  line("random playout, C++ API", "turns", rate(seconds, [&] {
         engine::BattleState state = position();
         const int start = state.turn;
         return static_cast<long>(search::playRandomly(game, state, ++seed).turns - start);
       }));
  line("copy + one turn, C++ API (one lookahead step)", "turns", rate(seconds, [&] {
         const engine::BattleState &from = position();
         engine::BattleState state = from;
         engine::MersenneRNG dice(++seed);
         rules.resolveTurn(state, rules.legalActions(from, 0).front(),
                           rules.legalActions(from, 1).front(), dice);
         return 1L;
       }));
  line("legal actions, both sides", "calls", rate(seconds, [&] {
         const engine::BattleState &state = position();
         return static_cast<long>(!rules.legalActions(state, 0).empty() +
                                  !rules.legalActions(state, 1).empty());
       }));
  line("observe", "calls", rate(seconds, [&] {
         const engine::BattleState view = engine::observe(position(), 0);
         return static_cast<long>(view.turn >= 0);
       }));
  std::vector<engine::BattleState> views;
  for (const engine::BattleState &state : positions)
    views.push_back(engine::observe(state, 0));
  auto view = [&]() -> const engine::BattleState & {
    next = (next + 1) % views.size();
    return views[next];
  };
  line("expected damage, first move", "calls", rate(seconds, [&] {
         return static_cast<long>(rules.expectedDamage(view(), 0, 0).max >= 0);
       }));
  std::vector<float> row(game.features().names().size());
  line("features of a position", "positions", rate(seconds, [&] {
         game.features().compute(view(), 0, row.data());
         return 1L;
       }));
  return 0;
}
