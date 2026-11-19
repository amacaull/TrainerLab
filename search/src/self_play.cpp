#include "search/self_play.hpp"

#include "search/encoding.hpp"

#include "engine/core/observation.hpp"
#include "engine/core/rng.hpp"

#include <algorithm>
#include <atomic>
#include <exception>
#include <mutex>
#include <random>
#include <stdexcept>
#include <thread>
#include <variant>

namespace search {
namespace {
using engine::Action;
using engine::BattleState;

size_t at(int i) { return static_cast<size_t>(i); }

int keyOf(const Action &action) {
  return std::holds_alternative<engine::UseMove>(action)
             ? std::get<engine::UseMove>(action).moveIndex
             : engine::kMaxMovesPerPokemon + std::get<engine::SwitchAction>(action).teamIndex;
}

// One battle played to its end.
SelfPlayData playOne(const Game &game, const Planner &planner, int maxTurns, uint64_t number,
                     int index) {
  const engine::BattleEngine &rules = game.rules();
  const Encoding &encoding = game.encoding();
  const size_t pokemonSize = Encoding::kSlots * encoding.pokemonNames().size();
  const size_t idsSize = Encoding::kSlots * Encoding::kIds;
  const size_t fieldSize = encoding.fieldNames().size();

  SelfPlayData data;
  std::vector<int> sides; // the side of each row, for its result
  BattleState state = game.randomBattle(number);
  engine::MersenneRNG dice(number);
  std::mt19937_64 draws(number ^ 0x9E3779B97F4A7C15ULL);
  uint64_t decision = 0;
  while (!state.isOver() && state.turn < maxTurns) {
    if (state.active(0).isFainted() || state.active(1).isFainted()) {
      const int first = rules.fasterSide(state, dice);
      const int side = state.active(first).isFainted() ? first : 1 - first;
      rules.resolveReplacement(state, side,
                               planner.chooseReplacement(engine::observe(state, side), side));
      continue;
    }
    std::array<Action, 2> chosen{engine::UseMove{0}, engine::UseMove{0}};
    for (int side : {0, 1}) {
      const std::vector<Action> legal = rules.legalActions(state, side);
      if (legal.size() == 1) {
        chosen[at(side)] = legal[0];
        continue;
      }
      const BattleState view = engine::observe(state, side);
      const auto counts = planner.visits(view, side, legal, number * 1'000'003 + decision++);
      int total = 0;
      for (int n : counts)
        total += n;
      // An option drawn in proportion to its visits.
      int left = static_cast<int>(draws() % static_cast<uint64_t>(total));
      for (const Action &action : legal) {
        left -= counts[at(keyOf(action))];
        if (left < 0) {
          chosen[at(side)] = action;
          break;
        }
      }
      const size_t row = at(data.rows());
      data.pokemon.resize((row + 1) * pokemonSize);
      data.ids.resize((row + 1) * idsSize);
      data.field.resize((row + 1) * fieldSize);
      encoding.encode(view, side, data.pokemon.data() + row * pokemonSize,
                      data.ids.data() + row * idsSize, data.field.data() + row * fieldSize);
      std::array<float, Encoding::kActions> mask{};
      Encoding::mask(legal, mask.data());
      data.mask.insert(data.mask.end(), mask.begin(), mask.end());
      for (int n : counts)
        data.policy.push_back(static_cast<float>(n) / static_cast<float>(total));
      data.result.push_back(0.0f);
      data.game.push_back(index);
      sides.push_back(side);
    }
    rules.resolveTurn(state, chosen[0], chosen[1], dice);
  }
  const bool lost0 = state.sideHasLost(0), lost1 = state.sideHasLost(1);
  const int winner = lost0 == lost1 ? -1 : (lost1 ? 0 : 1);
  for (size_t r = 0; r < sides.size(); ++r)
    data.result[r] = winner < 0 ? 0.0f : (sides[r] == winner ? 1.0f : -1.0f);
  data.turns.push_back(state.turn);
  data.winner.push_back(static_cast<int8_t>(winner));
  return data;
}

template <typename T> void append(std::vector<T> &to, const std::vector<T> &from) {
  to.insert(to.end(), from.begin(), from.end());
}
} // namespace

SelfPlayData selfPlay(const Game &game, const Network &network, const SelfPlaySettings &settings,
                      int games, uint64_t seed, int threads) {
  if (games < 0 || threads < 1 || settings.planner.simulations < 2)
    throw std::invalid_argument("selfPlay: games >= 0, threads >= 1, simulations >= 2");
  const Planner planner(game, network, settings.planner);
  std::vector<SelfPlayData> battles(at(games));
  std::atomic<int> next{0};
  std::exception_ptr failure;
  std::mutex failureLock;
  auto work = [&] {
    try {
      for (int g = next++; g < games; g = next++)
        battles[at(g)] = playOne(game, planner, settings.maxTurns,
                                 seed * 1'000'003 + static_cast<uint64_t>(g), g);
    } catch (...) {
      const std::lock_guard<std::mutex> lock(failureLock);
      failure = std::current_exception();
      next = games; // the others stop at their next battle
    }
  };
  std::vector<std::thread> pool;
  for (int t = 1; t < std::min(threads, std::max(games, 1)); ++t)
    pool.emplace_back(work);
  work();
  for (std::thread &thread : pool)
    thread.join();
  if (failure)
    std::rethrow_exception(failure);

  SelfPlayData all;
  for (const SelfPlayData &b : battles) {
    append(all.pokemon, b.pokemon);
    append(all.ids, b.ids);
    append(all.field, b.field);
    append(all.mask, b.mask);
    append(all.policy, b.policy);
    append(all.result, b.result);
    append(all.game, b.game);
    append(all.turns, b.turns);
    append(all.winner, b.winner);
  }
  return all;
}
} // namespace search
