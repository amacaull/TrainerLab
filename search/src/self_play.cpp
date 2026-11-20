#include "search/self_play.hpp"

#include "search/encoding.hpp"

#include "engine/core/observation.hpp"
#include "engine/core/rng.hpp"

#include <algorithm>
#include <atomic>
#include <exception>
#include <memory>
#include <mutex>
#include <random>
#include <span>
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

// One battle, played step by step as its searches wait on the network (palier 8.3): pending() is
// what it waits on, resume() gives it the outputs. The queries point into it: it stays where it is
// built.
class Battle {
public:
  Battle(const Game &game, const Planner &planner, int maxTurns, uint64_t number, int index)
      : game_(game), planner_(planner), maxTurns_(maxTurns), number_(number), index_(index),
        state_(game.randomBattle(number)), dice_(number), draws_(number ^ 0x9E3779B97F4A7C15ULL) {
    advance();
  }
  Battle(const Battle &) = delete;
  Battle &operator=(const Battle &) = delete;

  bool done() const { return done_; }
  std::span<const Network::Query> pending() const {
    return search_ ? search_->pending() : std::span<const Network::Query>(&query_, 1);
  }
  void resume(const Network::Output *outputs);
  SelfPlayData &data() { return data_; }

private:
  void advance();
  void record(); // the search's visits: a row, and the option drawn
  void finish();

  const Game &game_;
  const Planner &planner_;
  int maxTurns_;
  uint64_t number_;
  int index_;
  BattleState state_;
  engine::MersenneRNG dice_;
  std::mt19937_64 draws_;
  uint64_t decision_ = 0;
  SelfPlayData data_;
  std::vector<int> sides_; // the side of each row, for its result
  bool done_ = false;

  // The turn under way: whose choice is next (2: both chosen), and what they chose.
  int turnSide_ = 0;
  std::array<Action, 2> chosen_{engine::UseMove{0}, engine::UseMove{0}};
  std::vector<Action> legal_;
  BattleState view_;
  std::unique_ptr<Planner::Search> search_;
  // A fainted Pokemon to replace: the query for the network's favourite switch.
  int replacing_ = 0;
  BattleState replacingView_;
  std::array<float, Encoding::kActions> mask_{};
  Network::Query query_{};
};

// The battle, on until it waits on the network or is over: replacements first, then each side
// searches what it sees (a side with one legal option plays it, unrecorded), then the turn.
void Battle::advance() {
  const engine::BattleEngine &rules = game_.rules();
  while (true) {
    if (turnSide_ == 0) {
      if (state_.isOver() || state_.turn >= maxTurns_) {
        finish();
        return;
      }
      if (state_.active(0).isFainted() || state_.active(1).isFainted()) {
        const int first = rules.fasterSide(state_, dice_);
        replacing_ = state_.active(first).isFainted() ? first : 1 - first;
        replacingView_ = engine::observe(state_, replacing_);
        mask_ = Planner::replacementMask(replacingView_, replacing_);
        query_ = {&replacingView_, replacing_, mask_.data()};
        return;
      }
    }
    if (turnSide_ == 2) {
      rules.resolveTurn(state_, chosen_[0], chosen_[1], dice_);
      turnSide_ = 0;
      continue;
    }
    legal_ = rules.legalActions(state_, turnSide_);
    if (legal_.size() == 1) {
      chosen_[at(turnSide_)] = legal_[0];
      ++turnSide_;
      continue;
    }
    view_ = engine::observe(state_, turnSide_);
    search_ = std::make_unique<Planner::Search>(planner_, view_, turnSide_, legal_,
                                                number_ * 1'000'003 + decision_++);
    if (!search_->done())
      return;
    record();
  }
}

void Battle::resume(const Network::Output *outputs) {
  if (search_) {
    search_->resume(outputs);
    if (!search_->done())
      return;
    record();
  } else {
    game_.rules().resolveReplacement(state_, replacing_, Planner::pickReplacement(outputs[0]));
  }
  advance();
}

void Battle::record() {
  const Encoding &encoding = game_.encoding();
  const size_t pokemonSize = Encoding::kSlots * encoding.pokemonNames().size();
  const size_t idsSize = Encoding::kSlots * Encoding::kIds;
  const size_t fieldSize = encoding.fieldNames().size();
  const auto &counts = search_->analysis().visits;
  const int side = turnSide_;
  int total = 0;
  for (int n : counts)
    total += n;
  // An option drawn in proportion to its visits.
  int left = static_cast<int>(draws_() % static_cast<uint64_t>(total));
  for (const Action &action : legal_) {
    left -= counts[at(keyOf(action))];
    if (left < 0) {
      chosen_[at(side)] = action;
      break;
    }
  }
  const size_t row = at(data_.rows());
  data_.pokemon.resize((row + 1) * pokemonSize);
  data_.ids.resize((row + 1) * idsSize);
  data_.field.resize((row + 1) * fieldSize);
  encoding.encode(view_, side, data_.pokemon.data() + row * pokemonSize,
                  data_.ids.data() + row * idsSize, data_.field.data() + row * fieldSize);
  std::array<float, Encoding::kActions> mask{};
  Encoding::mask(legal_, mask.data());
  data_.mask.insert(data_.mask.end(), mask.begin(), mask.end());
  for (int n : counts)
    data_.policy.push_back(static_cast<float>(n) / static_cast<float>(total));
  data_.result.push_back(0.0f);
  data_.game.push_back(index_);
  sides_.push_back(side);
  search_.reset();
  ++turnSide_;
}

void Battle::finish() {
  const bool lost0 = state_.sideHasLost(0), lost1 = state_.sideHasLost(1);
  const int winner = lost0 == lost1 ? -1 : (lost1 ? 0 : 1);
  for (size_t r = 0; r < sides_.size(); ++r)
    data_.result[r] = winner < 0 ? 0.0f : (sides_[r] == winner ? 1.0f : -1.0f);
  data_.turns.push_back(state_.turn);
  data_.winner.push_back(static_cast<int8_t>(winner));
  done_ = true;
}

// Battles [first, first + count) side by side: each round, every battle not over gives what it
// waits on, all evaluated in one batch, in battle order.
void playTogether(const Game &game, const Network &network, const Planner &planner,
                  const SelfPlaySettings &settings, uint64_t seed, int first, int count,
                  std::vector<SelfPlayData> &battles) {
  std::vector<std::unique_ptr<Battle>> lanes;
  for (int g = first; g < first + count; ++g)
    lanes.push_back(std::make_unique<Battle>(game, planner, settings.maxTurns,
                                             seed * 1'000'003 + static_cast<uint64_t>(g), g));
  std::vector<Network::Query> queries;
  std::vector<Network::Output> outputs;
  while (true) {
    queries.clear();
    for (const auto &battle : lanes)
      if (!battle->done())
        for (const Network::Query &query : battle->pending())
          queries.push_back(query);
    if (queries.empty())
      break;
    outputs.resize(queries.size());
    network.evaluate(queries, outputs.data());
    const Network::Output *next = outputs.data();
    for (const auto &battle : lanes)
      if (!battle->done()) {
        const size_t size = battle->pending().size();
        battle->resume(next);
        next += size;
      }
  }
  for (int k = 0; k < count; ++k)
    battles[at(first + k)] = std::move(lanes[at(k)]->data());
}

template <typename T> void append(std::vector<T> &to, const std::vector<T> &from) {
  to.insert(to.end(), from.begin(), from.end());
}
} // namespace

SelfPlayData selfPlay(const Game &game, const Network &network, const SelfPlaySettings &settings,
                      int games, uint64_t seed, int threads) {
  if (games < 0 || threads < 1 || settings.planner.simulations < 2 || settings.lanes < 1)
    throw std::invalid_argument("selfPlay: games >= 0, threads >= 1, simulations >= 2, lanes >= 1");
  const Planner planner(game, network, settings.planner);
  std::vector<SelfPlayData> battles(at(games));
  const int groups = (games + settings.lanes - 1) / settings.lanes;
  std::atomic<int> next{0};
  std::exception_ptr failure;
  std::mutex failureLock;
  auto work = [&] {
    try {
      for (int g = next++; g < groups; g = next++) {
        const int first = g * settings.lanes;
        playTogether(game, network, planner, settings, seed, first,
                     std::min(settings.lanes, games - first), battles);
      }
    } catch (...) {
      const std::lock_guard<std::mutex> lock(failureLock);
      failure = std::current_exception();
      next = groups; // the others stop at their next group
    }
  };
  std::vector<std::thread> pool;
  for (int t = 1; t < std::min(threads, std::max(groups, 1)); ++t)
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
