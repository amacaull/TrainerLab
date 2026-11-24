#include "search/self_play.hpp"

#include "search/encoding.hpp"

#include "engine/core/observation.hpp"
#include "engine/core/rng.hpp"
#include "engine/core/switching.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
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

// The random battle's teams cut to a size drawn from the battle's number (palier 10.5).
BattleState sized(BattleState state, const SelfPlaySettings &settings, uint64_t number) {
  std::mt19937_64 draw(number ^ 0xA0761D6478BD642FULL);
  double left = static_cast<double>(draw() >> 11) * 0x1.0p-53;
  int size = settings.teamSizes.back().first;
  for (const auto &[teamSize, share] : settings.teamSizes) {
    left -= share;
    if (left < 0.0) {
      size = teamSize;
      break;
    }
  }
  for (int side : {0, 1})
    state.team_size[at(side)] = std::min(state.team_size[at(side)], size);
  return state;
}

// The result for `side` of a battle that is over: 1, -1, or 0 for a draw.
double endValue(const BattleState &state, int side) {
  const bool lost = state.sideHasLost(side), won = state.sideHasLost(1 - side);
  return lost == won ? 0.0 : (won ? 1.0 : -1.0);
}

// One battle, played step by step as its searches wait on the network (palier 8.3): pending() is
// what it waits on, resume() gives it the outputs. The queries point into it: it stays where it is
// built.
class Battle {
public:
  Battle(const Game &game, const Planner &full, const Planner &fast, const Planner &replacer,
         const SelfPlaySettings &settings, uint64_t number, int index)
      : game_(game), full_(full), fast_(fast), replacer_(replacer), settings_(settings),
        number_(number), index_(index), state_(sized(game.randomBattle(number), settings, number)),
        dice_(number), draws_(number ^ 0x9E3779B97F4A7C15ULL),
        caps_(number ^ 0xC2B2AE3D27D4EB4FULL) {
    advance();
  }
  // Palier 12.3: a measure's battle, from `start`, as Python's referee plays it (playMatches).
  Battle(const Game &game, const Planner &planner, const Planner &replacer,
         const SelfPlaySettings &settings, const BattleState &start, uint64_t seed, int index)
      : game_(game), full_(planner), fast_(planner), replacer_(replacer), settings_(settings),
        number_(seed), index_(index), state_(start), dice_(seed), draws_(seed), caps_(seed),
        measure_(true) {
    advance();
  }
  Battle(const Battle &) = delete;
  Battle &operator=(const Battle &) = delete;

  bool done() const { return done_; }
  std::span<const Network::Query> pending() const {
    if (search_)
      return search_->pending();
    if (candidateSearch_)
      return candidateSearch_->pending();
    return {&query_, 1};
  }
  void resume(const Network::Output *outputs);
  SelfPlayData &data() { return data_; }

private:
  void advance();
  void record(); // the option drawn from the search's visits; a row if the search was full
  size_t addRow(const BattleState &view, int side,
                const std::array<float, Encoding::kActions> &mask,
                const std::array<float, Encoding::kActions> &policy, double searched);
  bool nextCandidate(); // a replacement searched: on to the next option; true if it waits
  void sendReplacement();
  void finish();
  // The dice: self-play's, one stream; a measure's, a stream per turn, as Python's referee.
  engine::MersenneRNG &dice();

  const Game &game_;
  const Planner &full_, &fast_, &replacer_;
  const SelfPlaySettings &settings_;
  uint64_t number_;
  int index_;
  BattleState state_;
  engine::MersenneRNG dice_;
  std::mt19937_64 draws_;
  std::mt19937_64 caps_; // which searches are full
  uint64_t decision_ = 0;
  bool fullSearch_ = true;
  // A measure: the most visited option played, no rows, the dice of each turn on their own.
  bool measure_ = false;
  engine::MersenneRNG turnDice_{0};
  uint64_t replacementSeed_ = 0; // a measure's replacement searched: option k from this + k
  SelfPlayData data_;
  std::vector<int> sides_;       // the side of each row, for its result
  std::vector<size_t> turnRows_; // the rows of the turn under way, to learn what the foe played
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
  // A replacement searched (palier 10.4): each option played on the view, then searched (or its
  // network value taken when the side has one option left), in turn.
  bool searchingReplacement_ = false;
  std::vector<int> options_;
  std::vector<double> values_;
  BattleState after_;
  std::unique_ptr<Planner::Search> candidateSearch_;
};

// The battle, on until it waits on the network or is over: replacements first, then each side
// searches what it sees (a side with one legal option plays it, unrecorded), then the turn.
void Battle::advance() {
  const engine::BattleEngine &rules = game_.rules();
  while (true) {
    if (turnSide_ == 0) {
      if (state_.isOver() || state_.turn >= settings_.maxTurns) {
        finish();
        return;
      }
      if (state_.active(0).isFainted() || state_.active(1).isFainted()) {
        const int first = rules.fasterSide(state_, dice());
        replacing_ = state_.active(first).isFainted() ? first : 1 - first;
        replacingView_ = engine::observe(state_, replacing_);
        mask_ = Planner::replacementMask(replacingView_, replacing_);
        options_ = engine::validSwitchTargets(replacingView_, replacing_);
        if (settings_.replacementSimulations > 0 && options_.size() > 1) {
          searchingReplacement_ = true;
          values_.clear();
          replacementSeed_ = number_ * 1'000'003 + decision_++;
          if (nextCandidate())
            return;
          sendReplacement();
          continue;
        }
        query_ = {&replacingView_, replacing_, mask_.data()};
        return;
      }
    }
    if (turnSide_ == 2) {
      for (size_t row : turnRows_) {
        const int foe = 1 - sides_[row];
        data_.foe[row] = keyOf(chosen_[at(foe)]);
        Encoding::mask(rules.legalActions(state_, foe),
                       data_.foeMask.data() + row * Encoding::kActions);
      }
      turnRows_.clear();
      rules.resolveTurn(state_, chosen_[0], chosen_[1], dice());
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
    fullSearch_ =
        settings_.fast == 0 || static_cast<double>(caps_() >> 11) * 0x1.0p-53 < settings_.fullShare;
    search_ = std::make_unique<Planner::Search>(fullSearch_ ? full_ : fast_, view_, turnSide_,
                                                legal_, number_ * 1'000'003 + decision_++);
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
  } else if (searchingReplacement_) {
    if (candidateSearch_) {
      candidateSearch_->resume(outputs);
      if (!candidateSearch_->done())
        return;
      const Planner::Analysis &analysis = candidateSearch_->analysis();
      double value = 0.0;
      int total = 0;
      for (size_t a = 0; a < analysis.visits.size(); ++a) {
        value += analysis.visits[a] * analysis.value[a];
        total += analysis.visits[a];
      }
      values_.push_back(value / std::max(total, 1));
      candidateSearch_.reset();
    } else {
      values_.push_back(outputs[0].value); // the side had one option: the network's value
    }
    if (nextCandidate())
      return;
    sendReplacement();
  } else {
    game_.rules().resolveReplacement(state_, replacing_, Planner::pickReplacement(outputs[0]));
  }
  advance();
}

// The options not valued yet, one after the other, until one waits on the network.
bool Battle::nextCandidate() {
  const engine::BattleEngine &rules = game_.rules();
  while (values_.size() < options_.size()) {
    after_ = replacingView_;
    rules.resolveReplacement(after_, replacing_, options_[values_.size()]);
    if (after_.isOver()) {
      values_.push_back(endValue(after_, replacing_));
      continue;
    }
    const std::vector<Action> legal = rules.legalActions(after_, replacing_);
    if (legal.size() < 2) {
      Encoding::mask(legal, mask_.data());
      query_ = {&after_, replacing_, mask_.data()};
      return true;
    }
    const uint64_t seed =
        measure_ ? replacementSeed_ + values_.size() : number_ * 1'000'003 + decision_++;
    candidateSearch_ =
        std::make_unique<Planner::Search>(replacer_, after_, replacing_, legal, seed);
    if (!candidateSearch_->done())
      return true;
    candidateSearch_.reset(); // cannot happen with two options or more; kept safe
    values_.push_back(0.0);
  }
  return false;
}

// The best option sent in, and a row: the replacing side's view, its switches, the chosen one.
void Battle::sendReplacement() {
  size_t best = 0;
  for (size_t k = 1; k < values_.size(); ++k)
    if (values_[k] > values_[best])
      best = k;
  if (!measure_) {
    std::array<float, Encoding::kActions> policy{};
    policy[at(engine::kMaxMovesPerPokemon + options_[best])] = 1.0f;
    addRow(replacingView_, replacing_, Planner::replacementMask(replacingView_, replacing_), policy,
           values_[best]);
  }
  game_.rules().resolveReplacement(state_, replacing_, options_[best]);
  searchingReplacement_ = false;
}

size_t Battle::addRow(const BattleState &view, int side,
                      const std::array<float, Encoding::kActions> &mask,
                      const std::array<float, Encoding::kActions> &policy, double searched) {
  const Encoding &encoding = game_.encoding();
  const size_t pokemonSize = Encoding::kSlots * encoding.pokemonNames().size();
  const size_t idsSize = Encoding::kSlots * Encoding::kIds;
  const size_t fieldSize = encoding.fieldNames().size();
  const size_t row = at(data_.rows());
  data_.pokemon.resize((row + 1) * pokemonSize);
  data_.ids.resize((row + 1) * idsSize);
  data_.field.resize((row + 1) * fieldSize);
  encoding.encode(view, side, data_.pokemon.data() + row * pokemonSize,
                  data_.ids.data() + row * idsSize, data_.field.data() + row * fieldSize);
  data_.mask.insert(data_.mask.end(), mask.begin(), mask.end());
  data_.policy.insert(data_.policy.end(), policy.begin(), policy.end());
  data_.result.push_back(0.0f);
  data_.searchValue.push_back(static_cast<float>(searched));
  data_.foe.push_back(-1);
  data_.foeMask.resize(data_.foeMask.size() + Encoding::kActions, 0.0f);
  data_.game.push_back(index_);
  sides_.push_back(side);
  return row;
}

void Battle::record() {
  const Planner::Analysis &analysis = search_->analysis();
  const std::array<int, Encoding::kActions> counts = analysis.visits;
  double searched = 0.0;
  for (size_t a = 0; a < counts.size(); ++a)
    searched += counts[a] * analysis.value[a];
  const int side = turnSide_;
  int total = 0;
  for (int n : counts)
    total += n;
  if (measure_) { // the option visited most, the first of them (Planner::chooseAction)
    int bestVisits = -1;
    for (const Action &action : legal_)
      if (counts[at(keyOf(action))] > bestVisits) {
        bestVisits = counts[at(keyOf(action))];
        chosen_[at(side)] = action;
      }
    search_.reset();
    ++turnSide_;
    return;
  }
  // An option drawn in proportion to its visits.
  int left = static_cast<int>(draws_() % static_cast<uint64_t>(total));
  for (const Action &action : legal_) {
    left -= counts[at(keyOf(action))];
    if (left < 0) {
      chosen_[at(side)] = action;
      break;
    }
  }
  searched /= std::max(total, 1);
  search_.reset();
  ++turnSide_;
  if (!fullSearch_)
    return;
  std::array<float, Encoding::kActions> mask{}, policy{};
  Encoding::mask(legal_, mask.data());
  for (size_t a = 0; a < counts.size(); ++a)
    policy[a] = static_cast<float>(counts[a]) / static_cast<float>(total);
  turnRows_.push_back(addRow(view_, side, mask, policy, searched));
}

engine::MersenneRNG &Battle::dice() {
  if (!measure_)
    return dice_;
  turnDice_ = engine::MersenneRNG(number_ * 1'000'003 + static_cast<uint64_t>(state_.turn));
  return turnDice_;
}

void Battle::finish() {
  const bool lost0 = state_.sideHasLost(0), lost1 = state_.sideHasLost(1);
  const int winner = lost0 == lost1 ? -1 : (lost1 ? 0 : 1);
  for (size_t r = 0; r < sides_.size(); ++r)
    data_.result[r] = winner < 0 ? 0.0f : (sides_[r] == winner ? 1.0f : -1.0f);
  data_.turns.push_back(state_.turn);
  data_.winner.push_back(static_cast<int8_t>(winner));
  data_.teamSize.push_back(static_cast<int8_t>(state_.team_size[0]));
  done_ = true;
}

// Battles [first, first + count) side by side: each round, every battle not over gives what it
// waits on, all evaluated in one batch, in battle order.
template <typename Make>
void playTogether(const Network &network, int first, int count, const Make &make,
                  std::vector<SelfPlayData> &battles) {
  std::vector<std::unique_ptr<Battle>> lanes;
  for (int g = first; g < first + count; ++g)
    lanes.push_back(make(g));
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

// Groups of `lanes` battles shared among the threads: battles[g] for g in [0, games).
template <typename Make>
std::vector<SelfPlayData> playGroups(const Network &network, int games, int lanes, int threads,
                                     const Make &make) {
  std::vector<SelfPlayData> battles(at(games));
  const int groups = (games + lanes - 1) / lanes;
  std::atomic<int> next{0};
  std::exception_ptr failure;
  std::mutex failureLock;
  auto work = [&] {
    try {
      for (int g = next++; g < groups; g = next++) {
        const int first = g * lanes;
        playTogether(network, first, std::min(lanes, games - first), make, battles);
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
  return battles;
}
} // namespace

SelfPlayData selfPlay(const Game &game, const Network &network, const SelfPlaySettings &settings,
                      int games, uint64_t seed, int threads) {
  if (games < 0 || threads < 1 || settings.planner.simulations < 2 || settings.lanes < 1 ||
      settings.fast == 1 || settings.fast < 0 || !(settings.fullShare >= 0.0) ||
      settings.fullShare > 1.0 || settings.replacementSimulations == 1 ||
      settings.replacementSimulations < 0)
    throw std::invalid_argument(
        "selfPlay: games >= 0, threads >= 1, simulations >= 2, lanes "
        ">= 1, fast and replacement simulations 0 or >= 2, a share in [0, 1]");
  double shares = 0.0;
  for (const auto &[size, share] : settings.teamSizes) {
    if (size < 1 || size > engine::kTeamSize || !(share >= 0.0))
      throw std::invalid_argument("selfPlay: team sizes in [1, 6], shares >= 0");
    shares += share;
  }
  if (settings.teamSizes.empty() || std::abs(shares - 1.0) > 1e-9)
    throw std::invalid_argument("selfPlay: the team sizes' shares sum to 1");
  const Planner full(game, network, settings.planner);
  PlannerSettings quick = settings.planner;
  quick.simulations = settings.fast;
  quick.noise = 0.0;
  const Planner fast(game, network, quick);
  PlannerSettings replacing = quick;
  replacing.simulations = std::max(settings.replacementSimulations, 2);
  const Planner replacer(game, network, replacing);
  const std::vector<SelfPlayData> battles =
      playGroups(network, games, settings.lanes, threads, [&](int g) {
        const uint64_t number = seed * 1'000'003 + static_cast<uint64_t>(g);
        return std::make_unique<Battle>(game, full, fast, replacer, settings, number, g);
      });

  SelfPlayData all;
  for (const SelfPlayData &b : battles) {
    append(all.pokemon, b.pokemon);
    append(all.ids, b.ids);
    append(all.field, b.field);
    append(all.mask, b.mask);
    append(all.policy, b.policy);
    append(all.result, b.result);
    append(all.searchValue, b.searchValue);
    append(all.foe, b.foe);
    append(all.foeMask, b.foeMask);
    append(all.game, b.game);
    append(all.turns, b.turns);
    append(all.winner, b.winner);
    append(all.teamSize, b.teamSize);
  }
  return all;
}

MatchResult playMatches(const Game &game, const Network &network, const MatchSettings &settings,
                        const std::vector<BattleState> &starts, const std::vector<uint64_t> &seeds,
                        int threads) {
  if (starts.size() != seeds.size() || threads < 1 || settings.planner.simulations < 2 ||
      settings.lanes < 1 || settings.replacementSimulations == 1 ||
      settings.replacementSimulations < 0)
    throw std::invalid_argument("playMatches: a seed per battle, threads >= 1, simulations >= 2, "
                                "lanes >= 1, replacement simulations 0 or >= 2");
  SelfPlaySettings played;
  played.planner = settings.planner;
  played.maxTurns = settings.maxTurns;
  played.lanes = settings.lanes;
  played.fast = 0;
  played.replacementSimulations = settings.replacementSimulations;
  const Planner planner(game, network, settings.planner);
  PlannerSettings replacing = settings.planner;
  replacing.simulations = std::max(settings.replacementSimulations, 2);
  replacing.noise = 0.0;
  replacing.minRootVisits = 0;
  const Planner replacer(game, network, replacing);
  const std::vector<SelfPlayData> battles =
      playGroups(network, static_cast<int>(starts.size()), settings.lanes, threads, [&](int g) {
        return std::make_unique<Battle>(game, planner, replacer, played, starts[at(g)],
                                        seeds[at(g)], g);
      });
  MatchResult result;
  for (const SelfPlayData &b : battles) {
    append(result.winner, b.winner);
    append(result.turns, b.turns);
  }
  return result;
}
} // namespace search
