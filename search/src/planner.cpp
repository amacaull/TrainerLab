#include "search/planner.hpp"

#include "search/encoding.hpp"
#include "search/mcts.hpp"

#include "engine/core/observation.hpp"
#include "engine/core/switching.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <memory>
#include <random>
#include <utility>
#include <variant>

namespace search {
namespace {
using engine::Action;
using engine::BattleState;

constexpr int kMoves = engine::kMaxMovesPerPokemon;

size_t at(int i) { return static_cast<size_t>(i); }

// An option's place among the ten: a move by its slot, a switch by 4 + its team index.
int keyOf(const Action &action) {
  return std::holds_alternative<engine::UseMove>(action)
             ? std::get<engine::UseMove>(action).moveIndex
             : kMoves + std::get<engine::SwitchAction>(action).teamIndex;
}

// The result for `root` of a battle that is over: 1, -1, or 0 for a draw.
double endValue(const BattleState &state, int root) {
  const bool lost = state.sideHasLost(root), won = state.sideHasLost(1 - root);
  return lost == won ? 0.0 : (won ? 1.0 : -1.0);
}
} // namespace

// Per side, the prior, visits and summed value (for that side) of each of the ten options.
struct Planner::Node {
  bool expanded = false;
  int visits = 0;
  double value = 0.0; // the network's value, for the root
  std::array<std::array<double, Encoding::kActions>, 2> prior{};
  std::array<std::array<int, Encoding::kActions>, 2> n{};
  std::array<std::array<double, Encoding::kActions>, 2> w{};
  std::map<std::pair<int, int>, std::unique_ptr<Node>> children;
};

Planner::Planner(const Game &game, const Network &network, PlannerSettings settings)
    : game_(game), network_(network), settings_(settings) {}

int Planner::chooseReplacement(const BattleState &view, int side) const {
  std::array<float, Encoding::kActions> mask{};
  for (int index : engine::validSwitchTargets(view, side))
    mask[at(kMoves + index)] = 1.0f;
  const Network::Output out = network_.evaluate(view, side, mask.data());
  const auto best = std::max_element(out.logits.begin(), out.logits.end());
  return static_cast<int>(best - out.logits.begin()) - kMoves;
}

void Planner::replaceFainted(BattleState &state, engine::RNG &dice) const {
  while (!state.isOver() && (state.active(0).isFainted() || state.active(1).isFainted())) {
    const int first = game_.rules().fasterSide(state, dice);
    const int side = state.active(first).isFainted() ? first : 1 - first;
    game_.rules().resolveReplacement(state, side,
                                     chooseReplacement(engine::observe(state, side), side));
  }
}

double Planner::expand(const BattleState &state, Node &node, int root) const {
  // Both sides' policies, in one batch; the value is the mean of what each side's network
  // expects, for root.
  std::array<std::array<float, Encoding::kActions>, 2> masks{};
  const std::array<BattleState, 2> views{engine::observe(state, 0), engine::observe(state, 1)};
  std::array<Network::Query, 2> queries{};
  for (int side : {0, 1}) {
    Encoding::mask(game_.rules().legalActions(state, side), masks[at(side)].data());
    queries[at(side)] = {&views[at(side)], side, masks[at(side)].data()};
  }
  std::array<Network::Output, 2> outputs{};
  network_.evaluate(queries, outputs.data());

  double value = 0.0;
  for (int side : {0, 1}) {
    const Network::Output &out = outputs[at(side)];
    const float top = *std::max_element(out.logits.begin(), out.logits.end());
    double total = 0.0;
    for (int a = 0; a < Encoding::kActions; ++a) {
      const double p = masks[at(side)][at(a)] == 1.0f ? std::exp(out.logits[at(a)] - top) : 0.0;
      node.prior[at(side)][at(a)] = p;
      total += p;
    }
    for (double &p : node.prior[at(side)])
      p /= total;
    value += (side == root ? out.value : -out.value) / 2;
  }
  node.expanded = true;
  node.value = value;
  return value;
}

double Planner::simulate(BattleState &state, Node &node, int root, engine::RNG &dice,
                         bool atRoot) const {
  replaceFainted(state, dice);
  if (state.isOver())
    return endValue(state, root);
  if (!node.expanded)
    return expand(state, node, root);

  std::array<Action, 2> chosen{engine::UseMove{0}, engine::UseMove{0}};
  std::array<int, 2> keys{};
  const double sqrtVisits = std::sqrt(static_cast<double>(std::max(node.visits, 1)));
  for (int side : {0, 1}) {
    const double own = side == root ? node.value : -node.value;
    const bool judged = atRoot && side == root && settings_.minRootVisits > 0;
    double best = -1e18;
    for (const Action &action : game_.rules().legalActions(state, side)) {
      const int k = keyOf(action);
      const int visits = node.n[at(side)][at(k)];
      const double q = visits ? node.w[at(side)][at(k)] / visits : own;
      double score =
          q + settings_.explore * node.prior[at(side)][at(k)] * sqrtVisits / (1 + visits);
      if (judged && visits < settings_.minRootVisits)
        score = 1e9 - visits; // the least tried first, until each has its minimum
      if (score > best)
        best = score, chosen[at(side)] = action, keys[at(side)] = k;
    }
  }

  std::unique_ptr<Node> &child = node.children[{keys[0], keys[1]}];
  if (child == nullptr)
    child = std::make_unique<Node>();
  game_.rules().resolveTurn(state, chosen[0], chosen[1], dice);
  const double value = simulate(state, *child, root, dice, false);

  ++node.visits;
  for (int side : {0, 1}) {
    ++node.n[at(side)][at(keys[at(side)])];
    node.w[at(side)][at(keys[at(side)])] += side == root ? value : -value;
  }
  return value;
}

std::array<int, Encoding::kActions> Planner::visits(const BattleState &view, int side,
                                                    const std::vector<Action> &legal,
                                                    uint64_t seed) const {
  return analyse(view, side, legal, seed).visits;
}

Planner::Analysis Planner::analyse(const BattleState &view, int side,
                                   const std::vector<Action> &legal, uint64_t seed) const {
  Analysis analysis;
  if (legal.size() == 1) {
    analysis.visits[at(keyOf(legal[0]))] = 1;
    return analysis;
  }
  const std::vector<BattleState> guesses =
      guessWorlds(game_, view, 1 - side, settings_.worlds, seed);
  Node root;
  for (int i = 0; i < settings_.simulations; ++i) {
    BattleState state = guesses[at(i % settings_.worlds)];
    engine::MersenneRNG dice(seed * 1'000'003 + static_cast<uint64_t>(i));
    simulate(state, root, side, dice, true);
    if (i == 0 && settings_.noise > 0.0)
      addNoise(root.prior[at(side)], legal, seed);
  }
  for (const Action &action : legal) {
    const size_t k = at(keyOf(action));
    analysis.visits[k] = root.n[at(side)][k];
    analysis.value[k] = analysis.visits[k] ? root.w[at(side)][k] / analysis.visits[k] : 0.0;
  }
  return analysis;
}

void Planner::addNoise(std::array<double, Encoding::kActions> &prior,
                       const std::vector<Action> &legal, uint64_t seed) const {
  // A flat Dirichlet draw: independent exponentials, normalised. Alpha = 1 is AlphaZero's
  // scaling (about 10 over the number of options) for the few options of a turn here.
  std::mt19937_64 rng(seed ^ 0xD1B54A32D192ED03ULL);
  std::array<double, Encoding::kActions> draw{};
  double total = 0.0;
  for (const Action &action : legal) {
    const double u = static_cast<double>((rng() >> 11) + 1) * 0x1.0p-53; // in (0, 1]
    draw[at(keyOf(action))] = -std::log(u);
    total += draw[at(keyOf(action))];
  }
  for (const Action &action : legal) {
    const size_t k = at(keyOf(action));
    prior[k] = (1 - settings_.noise) * prior[k] + settings_.noise * draw[k] / total;
  }
}

int Planner::chooseAction(const BattleState &view, int side, const std::vector<Action> &legal,
                          uint64_t seed) const {
  const std::array<int, Encoding::kActions> counts = visits(view, side, legal, seed);
  int best = 0, bestVisits = -1;
  for (size_t i = 0; i < legal.size(); ++i) {
    const int n = counts[at(keyOf(legal[i]))];
    if (n > bestVisits)
      best = static_cast<int>(i), bestVisits = n;
  }
  return best;
}
} // namespace search
