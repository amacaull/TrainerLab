#include "search/forecaster.hpp"

#include "engine/core/observation.hpp"
#include "engine/core/rng.hpp"
#include "engine/ffi/ffi.hpp"

#include <algorithm>
#include <cmath>
#include <variant>

namespace search {
namespace {
using engine::Action;
using engine::BattlePokemon;
using engine::BattleState;

constexpr int kSamples = 2;           // turns played per option, on the same seeds for every option
constexpr double kWorthSaving = 0.5;  // share of its HP a threatened Pokemon must keep (rule 4)
constexpr double kCheapHit = 1.0 / 3; // share of its HP a switch-in may lose to the hit (rule 4)

size_t at(int i) { return static_cast<size_t>(i); }

bool isMove(const Action &action) { return std::holds_alternative<engine::UseMove>(action); }
int indexOf(const Action &action) {
  return isMove(action) ? std::get<engine::UseMove>(action).moveIndex
                        : std::get<engine::SwitchAction>(action).teamIndex;
}
} // namespace

double WinModel::predict(const float *features) const {
  std::vector<double> z(mean.size());
  for (size_t i = 0; i < z.size(); ++i)
    z[i] = (static_cast<double>(features[i]) - mean[i]) / std[i];
  for (size_t layer = 0; layer < weights.size(); ++layer) {
    const std::vector<double> &w = weights[layer];
    const std::vector<double> &b = biases[layer];
    const size_t outputs = b.size();
    std::vector<double> next(outputs, 0.0);
    for (size_t j = 0; j < outputs; ++j) {
      double sum = 0.0;
      for (size_t i = 0; i < z.size(); ++i)
        sum += z[i] * w[i * outputs + j];
      next[j] = sum + b[j];
    }
    if (layer + 1 < weights.size())
      for (double &value : next)
        value = std::max(value, 0.0);
    z = std::move(next);
  }
  return 1.0 / (1.0 + std::exp(-std::clamp(z[0], -30.0, 30.0)));
}

Forecaster::Forecaster(const Game &game, WinModel model) : game_(game), model_(std::move(model)) {}

BattleState Forecaster::playable(const BattleState &view, int foe) const {
  BattleState state = view;
  const BattlePokemon standIn = engine::ffi::make_combatant(view.active(foe).species_id);
  for (int i = 0; i < view.team_size[at(foe)]; ++i)
    if (view.teams[at(foe)][at(i)].isEmpty())
      state.teams[at(foe)][at(i)] = standIn;
  return state;
}

double Forecaster::odds(const BattleState &state, int side) const {
  const bool lost = state.sideHasLost(side);
  const bool won = state.sideHasLost(1 - side);
  if (lost || won)
    return lost && won ? 0.5 : (won ? 1.0 : 0.0); // the battle is over: no need to guess
  std::vector<float> row(game_.features().names().size());
  game_.features().compute(engine::observe(state, side), side, row.data());
  return model_.predict(row.data());
}

double Forecaster::afterTurn(const BattleState &state, int side, const Action &action,
                             const Action &foeAction) const {
  const Action &a0 = side == 0 ? action : foeAction;
  const Action &a1 = side == 0 ? foeAction : action;
  double total = 0.0;
  for (int sample = 0; sample < kSamples; ++sample) {
    BattleState after = state;
    engine::MersenneRNG rng(static_cast<uint64_t>(state.turn * kSamples + sample));
    game_.rules().resolveTurn(after, a0, a1, rng);
    total += odds(after, side);
  }
  return total / kSamples;
}

Action Forecaster::calculatorAction(const BattleState &view, int side,
                                    const std::vector<Action> &legal) const {
  const Estimate &estimate = game_.estimate();
  const BattlePokemon &active = view.active(side);
  const BattlePokemon &foe = view.active(1 - side);

  // Rule 4's escape: away from a healthy Pokemon the foe would knock out before it acts.
  if (active.currentHp >= kWorthSaving * active.stats.hp &&
      estimate.effectiveSpeed(foe) > estimate.effectiveSpeed(active) &&
      estimate.strongestHit(foe, active) >= active.currentHp) {
    const Action *best = nullptr;
    double bestMatchup = 0.0;
    for (const Action &action : legal) {
      if (isMove(action))
        continue;
      const BattlePokemon &incoming = view.teams[at(side)][at(indexOf(action))];
      if (estimate.strongestHit(foe, incoming) > kCheapHit * incoming.currentHp)
        continue;
      const double matchup = estimate.bestPower(incoming, foe.species_id) -
                             estimate.bestPower(foe, incoming.species_id);
      if (best == nullptr || matchup > bestMatchup)
        best = &action, bestMatchup = matchup;
    }
    if (best != nullptr)
      return *best;
  }

  // Rule 5's move: the most damage the engine expects.
  const Action *best = nullptr;
  double bestMean = 0.0;
  for (const Action &action : legal) {
    if (!isMove(action))
      continue;
    const double mean = game_.rules().expectedDamage(view, side, indexOf(action)).mean;
    if (best == nullptr || mean > bestMean)
      best = &action, bestMean = mean;
  }
  return *best;
}

int Forecaster::chooseAction(const BattleState &view, int side,
                             const std::vector<Action> &legal) const {
  const int foe = 1 - side;
  const BattleState state = playable(view, foe);
  const Action foeAction =
      calculatorAction(engine::observe(state, foe), foe, game_.rules().legalActions(state, foe));
  int best = 0;
  double bestValue = 0.0;
  for (size_t i = 0; i < legal.size(); ++i) {
    const double value = afterTurn(state, side, legal[i], foeAction);
    if (i == 0 || value > bestValue)
      best = static_cast<int>(i), bestValue = value;
  }
  return best;
}

int Forecaster::chooseStrategistAction(const BattleState &view, int side,
                                       const std::vector<Action> &legal) const {
  const int foe = 1 - side;
  const BattleState state = playable(view, foe);
  // The foe's moves, and its switches to Pokemon already seen: a stand-in is nothing to reason on.
  std::vector<Action> replies;
  for (const Action &reply : game_.rules().legalActions(state, foe))
    if (isMove(reply) || !view.teams[at(foe)][at(indexOf(reply))].isEmpty())
      replies.push_back(reply);

  int best = 0;
  double bestValue = -1.0;
  for (size_t i = 0; i < legal.size(); ++i) {
    double worst = 2.0;
    for (const Action &reply : replies) {
      worst = std::min(worst, afterTurn(state, side, legal[i], reply));
      if (worst <= bestValue)
        break; // already no better than the best option: its worst case is lower
    }
    if (worst > bestValue)
      best = static_cast<int>(i), bestValue = worst;
  }
  return best;
}

int Forecaster::chooseReplacement(const BattleState &view, int side,
                                  const std::vector<int> &options) const {
  const BattleState state = playable(view, 1 - side);
  int best = 0;
  double bestValue = 0.0;
  for (size_t i = 0; i < options.size(); ++i) {
    BattleState after = state;
    game_.rules().resolveReplacement(after, side, options[i]);
    const double value = odds(after, side);
    if (i == 0 || value > bestValue)
      best = static_cast<int>(i), bestValue = value;
  }
  return best;
}
} // namespace search
