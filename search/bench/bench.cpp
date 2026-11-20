// How fast the engine plays, through each of its two APIs: the reference that the C++ search of
// palier 5 is measured against.
//
//   search_bench            # about 2 s per line
//   search_bench 0.2        # seconds per line (a quarter of it per matrix product line)
//
// Every line plays from the same 64 positions as `trainerlab bench` (search::openings): random
// battles, 3 random turns in. A line reports how many times per second it does its one thing.

#include "search/game.hpp"
#include "search/planner.hpp"
#include "search/self_play.hpp"
#include "search/vec_env.hpp"

#include "engine/core/observation.hpp"
#include "engine/ffi/ffi.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <random>
#include <string>
#include <thread>
#include <vector>

#include "../tests/random_network.hpp"

#if defined(__APPLE__)
#define ACCELERATE_NEW_LAPACK
#include <Accelerate/Accelerate.h>
#endif

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

// The same on `threads` threads at once: step(thread) on each until `seconds` have passed.
double rateOnThreads(int threads, double seconds, const std::function<long(int)> &step) {
  std::atomic<long> units{0};
  const auto start = Clock::now();
  std::vector<std::thread> pool;
  for (int k = 0; k < threads; ++k)
    pool.emplace_back([&, k] {
      long mine = 0;
      do
        mine += step(k);
      while (std::chrono::duration<double>(Clock::now() - start).count() < seconds);
      units += mine;
    });
  for (std::thread &thread : pool)
    thread.join();
  const std::chrono::duration<double> elapsed = Clock::now() - start;
  return static_cast<double>(units) / elapsed.count();
}

void line(const char *what, const char *unit, double perSecond) {
  std::printf("%-44s %12.0f %s/s\n", what, perSecond, unit);
}

// Palier 8.1: our kernel against Accelerate's matrix product (the M4's matrix unit), at the sizes
// of two of the network's layers. Accelerate only pays on large products: these lines say from
// how many positions per batch, if any, batching across games (8.4) would be worth it.
void products(double seconds) {
  struct Layer {
    const char *name;
    int rowsPerPosition, in, out;
  };
  const Layer layers[] = {{"pokemon layer", 12, 265, 128}, {"trunk layer", 1, 529, 256}};
  std::printf("\n%-28s %9s %12s %12s\n", "matrix product (GMAC/s)", "positions", "ours",
              "Accelerate");
  std::mt19937 dice(1);
  std::uniform_real_distribution<float> uniform(-1.0f, 1.0f);
  for (const Layer &layer : layers)
    for (int positions : {1, 2, 16, 64}) {
      const int rows = layer.rowsPerPosition * positions;
      std::vector<float> x(static_cast<size_t>(rows) * static_cast<size_t>(layer.in));
      std::vector<float> w(static_cast<size_t>(layer.in) * static_cast<size_t>(layer.out));
      std::vector<float> y(static_cast<size_t>(rows) * static_cast<size_t>(layer.out));
      for (float &v : x)
        v = uniform(dice);
      for (float &v : w)
        v = uniform(dice);
      const double macs = double(rows) * layer.in * layer.out;
      const double ours = rate(seconds, [&] {
        search::multiply(x.data(), rows, layer.in, w.data(), layer.in, layer.out, y.data());
        return 1L;
      });
      std::printf("%-28s %9d %12.1f", layer.name, positions, ours * macs / 1e9);
#if defined(__APPLE__)
      const double theirs = rate(seconds, [&] {
        cblas_sgemm(CblasRowMajor, CblasNoTrans, CblasNoTrans, rows, layer.out, layer.in, 1.0f,
                    x.data(), layer.in, w.data(), layer.out, 0.0f, y.data(), layer.out);
        return 1L;
      });
      std::printf(" %12.1f\n", theirs * macs / 1e9);
#else
      std::printf(" %12s\n", "-");
#endif
    }
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
  std::printf("%zu positions, %.1f s per line, layers multiplied by %s\n\n", positions.size(),
              seconds, search::Network::backend());

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
  const search::Encoding &encoding = game.encoding();
  std::vector<float> slots(search::Encoding::kSlots * encoding.pokemonNames().size());
  std::vector<int32_t> ids(search::Encoding::kSlots * search::Encoding::kIds);
  std::vector<float> fieldRow(encoding.fieldNames().size());
  line("encoding of a position (palier 6)", "positions", rate(seconds, [&] {
         encoding.encode(view(), 0, slots.data(), ids.data(), fieldRow.data());
         return 1L;
       }));
  {
    constexpr int kBattles = 256;
    search::VecEnv env(game, kBattles, 1);
    const size_t sides = 2 * kBattles;
    std::vector<float> envPokemon(sides * slots.size()), envField(sides * fieldRow.size());
    std::vector<int32_t> envIds(sides * ids.size());
    std::vector<float> envMask(sides * search::Encoding::kActions), rewards(sides);
    std::vector<uint8_t> done(kBattles);
    std::vector<int32_t> actions(sides);
    std::mt19937_64 pick(1);
    line("vector env, 256 battles: observe + step", "steps", rate(seconds, [&] {
           env.observe(envPokemon.data(), envIds.data(), envField.data(), envMask.data());
           for (size_t k = 0; k < sides; ++k) {
             int legal[search::Encoding::kActions], n = 0;
             for (int o = 0; o < search::Encoding::kActions; ++o)
               if (envMask[k * search::Encoding::kActions + static_cast<size_t>(o)] == 1.0f)
                 legal[n++] = o;
             actions[k] = n ? legal[pick() % static_cast<uint64_t>(n)] : 0;
           }
           env.step(actions.data(), rewards.data(), done.data());
           return static_cast<long>(kBattles);
         }));
  }
  std::vector<float> row(game.features().names().size());
  line("features of a position", "positions", rate(seconds, [&] {
         game.features().compute(view(), 0, row.data());
         return 1L;
       }));
  {
    // Palier 6.5: random weights of the network's shapes, as fast as trained ones.
    const search::Network network = search::testing::randomNetwork(game);
    std::vector<std::vector<float>> masks;
    for (const engine::BattleState &state : positions) {
      std::vector<float> mask(search::Encoding::kActions);
      search::Encoding::mask(rules.legalActions(state, 0), mask.data());
      masks.push_back(mask);
    }
    line("network evaluation (palier 6.5)", "positions", rate(seconds, [&] {
           const engine::BattleState &v = view();
           return static_cast<long>(network.evaluate(v, 0, masks[next].data()).value < 2.0f);
         }));
    for (int size : {2, 16}) {
      std::vector<search::Network::Query> batch(static_cast<size_t>(size));
      std::vector<search::Network::Output> outputs(batch.size());
      const std::string what = "network evaluation, batches of " + std::to_string(size);
      line(what.c_str(), "positions", rate(seconds, [&] {
             for (auto &query : batch) {
               const engine::BattleState &v = view();
               query = {&v, 0, masks[next].data()};
             }
             network.evaluate(batch, outputs.data());
             return static_cast<long>(size);
           }));
    }
    const search::Planner planner(game, network);
    line("planner decision, 200 simulations (rule 11)", "decisions", rate(seconds, [&] {
           const engine::BattleState &v = view();
           const auto legal = rules.legalActions(positions[next], 0);
           return static_cast<long>(planner.chooseAction(v, 0, legal, ++seed) >= 0);
         }));
    // Palier 8.2: as self-play plays, on every core. The M4's matrix units are shared by its
    // cores: what Accelerate gains on one thread may not hold on all of them.
    const int cores = static_cast<int>(std::max(1u, std::thread::hardware_concurrency()));
    std::vector<size_t> where(static_cast<size_t>(cores));
    const std::string what = "planner decisions, " + std::to_string(cores) + " threads";
    line(what.c_str(), "decisions", rateOnThreads(cores, seconds, [&](int k) {
           size_t &i = where[static_cast<size_t>(k)];
           i = (i + 7) % positions.size();
           const auto legal = rules.legalActions(positions[i], 0);
           return static_cast<long>(planner.chooseAction(views[i], 0, legal, i * 31 + 1) >= 0);
         }));
    // Palier 8.3: self-play as the loop plays it, on every core, its battles alone or side by
    // side with their queries batched. Ten turns per battle: enough decisions to time, and
    // `lanes` battles for each core.
    for (int lanes : {1, 16}) {
      search::SelfPlaySettings settings; // every search rule 11's 200 simulations, as in 8.3
      settings.planner.simulations = 200;
      settings.fast = 0;
      settings.maxTurns = 10;
      settings.lanes = lanes;
      const std::string selfPlay =
          "self-play, " + std::to_string(cores) + " threads, " + std::to_string(lanes) + " lanes";
      line(selfPlay.c_str(), "decisions", rate(seconds, [&] {
             return static_cast<long>(
                 search::selfPlay(game, network, settings, cores * 16, ++seed, cores).rows());
           }));
    }
    // Palier 10: the same with 2 layers of attention over the Pokemon and the field.
    const search::Network attention = search::testing::randomNetwork(game, 2);
    std::vector<search::Network::Query> batch(16);
    std::vector<search::Network::Output> outputs(batch.size());
    line("attention network evaluation, batches of 16", "positions", rate(seconds, [&] {
           for (auto &query : batch) {
             const engine::BattleState &v = view();
             query = {&v, 0, masks[next].data()};
           }
           attention.evaluate(batch, outputs.data());
           return 16L;
         }));
    const search::Planner attentive(game, attention);
    line("attention planner decision, 200 simulations", "decisions", rate(seconds, [&] {
           const engine::BattleState &v = view();
           const auto legal = rules.legalActions(positions[next], 0);
           return static_cast<long>(attentive.chooseAction(v, 0, legal, ++seed) >= 0);
         }));
    search::SelfPlaySettings settings; // as the lines of 8.3
    settings.planner.simulations = 200;
    settings.fast = 0;
    settings.maxTurns = 10;
    const std::string selfPlay =
        "attention self-play, " + std::to_string(cores) + " threads, 16 lanes";
    line(selfPlay.c_str(), "decisions", rate(seconds, [&] {
           return static_cast<long>(
               search::selfPlay(game, attention, settings, cores * 16, ++seed, cores).rows());
         }));
  }
  products(seconds / 4);
  return 0;
}
