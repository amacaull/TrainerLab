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

std::array<float, Encoding::kActions> Planner::replacementMask(const BattleState &view, int side) {
  std::array<float, Encoding::kActions> mask{};
  for (int index : engine::validSwitchTargets(view, side))
    mask[at(kMoves + index)] = 1.0f;
  return mask;
}

int Planner::pickReplacement(const Network::Output &out) {
  const auto best = std::max_element(out.logits.begin(), out.logits.end());
  return static_cast<int>(best - out.logits.begin()) - kMoves;
}

int Planner::chooseReplacement(const BattleState &view, int side) const {
  const std::array<float, Encoding::kActions> mask = replacementMask(view, side);
  return pickReplacement(network_.evaluate(view, side, mask.data()));
}

int Planner::searchReplacement(const BattleState &view, int side, const std::vector<int> &options,
                               uint64_t seed, int simulations) const {
  PlannerSettings settings = settings_;
  settings.simulations = simulations;
  settings.noise = 0.0;
  settings.minRootVisits = 0;
  const Planner searcher(game_, network_, settings);
  int best = options.front();
  double bestValue = -2.0;
  for (size_t k = 0; k < options.size(); ++k) {
    BattleState after = view;
    game_.rules().resolveReplacement(after, side, options[k]);
    double value = 0.0;
    if (after.isOver()) {
      value = endValue(after, side);
    } else if (const std::vector<Action> legal = game_.rules().legalActions(after, side);
               legal.size() < 2 || simulations < 2) {
      std::array<float, Encoding::kActions> mask{};
      Encoding::mask(legal, mask.data());
      value = network_.evaluate(after, side, mask.data()).value;
    } else {
      const Analysis analysis = searcher.analyse(after, side, legal, seed + k);
      int total = 0;
      for (size_t a = 0; a < analysis.visits.size(); ++a) {
        value += analysis.visits[a] * analysis.value[a];
        total += analysis.visits[a];
      }
      value /= std::max(total, 1);
    }
    if (value > bestValue)
      bestValue = value, best = options[k];
  }
  return best;
}

// Both sides' policies, given in one batch: the node's priors; its value is the mean of what each
// side's network expects, for root.
double Planner::expand(Node &node,
                       const std::array<std::array<float, Encoding::kActions>, 2> &masks,
                       const Network::Output *outputs, int root) const {
  double value = 0.0;
  for (int side : {0, 1}) {
    const Network::Output &out = outputs[side];
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

Planner::Search::Search(const Planner &planner, const BattleState &view, int side,
                        const std::vector<Action> &legal, uint64_t seed)
    : planner_(planner), side_(side), legal_(legal), seed_(seed), root_(std::make_unique<Node>()) {
  if (legal.size() == 1) {
    analysis_.visits[at(keyOf(legal[0]))] = 1;
    done_ = true;
    return;
  }
  guesses_ = guessWorlds(planner.game_, view, 1 - side, planner.settings_.worlds, seed);
  advance();
}

Planner::Search::~Search() = default;

// Simulations, one after the other, until one waits on the network or all are done.
void Planner::Search::advance() {
  const PlannerSettings &settings = planner_.settings_;
  while (true) {
    if (!simulating_) {
      if (simulation_ == settings.simulations) {
        for (const Action &action : legal_) {
          const size_t k = at(keyOf(action));
          analysis_.visits[k] = root_->n[at(side_)][k];
          analysis_.value[k] =
              analysis_.visits[k] ? root_->w[at(side_)][k] / analysis_.visits[k] : 0.0;
        }
        done_ = true;
        return;
      }
      state_ = guesses_[at(simulation_ % settings.worlds)];
      dice_ = engine::MersenneRNG(seed_ * 1'000'003 + static_cast<uint64_t>(simulation_));
      node_ = root_.get();
      path_.clear();
      simulating_ = true;
    }
    if (descend())
      return;
    endSimulation();
  }
}

// Down the tree from where the simulation stands: a fainted Pokemon is replaced by the network's
// favourite switch; a battle over gives its result; a node never visited, the network's value.
// Each side otherwise picks by PUCT, and the pair of choices leads to the child.
bool Planner::Search::descend() {
  const engine::BattleEngine &rules = planner_.game_.rules();
  const PlannerSettings &settings = planner_.settings_;
  while (true) {
    if (!state_.isOver() && (state_.active(0).isFainted() || state_.active(1).isFainted())) {
      const int first = rules.fasterSide(state_, dice_);
      replacing_ = state_.active(first).isFainted() ? first : 1 - first;
      views_[0] = engine::observe(state_, replacing_);
      masks_[0] = replacementMask(views_[0], replacing_);
      queries_[0] = {&views_[0], replacing_, masks_[0].data()};
      pending_ = 1;
      waiting_ = Waiting::replacement;
      return true;
    }
    if (state_.isOver()) {
      backUp(endValue(state_, side_));
      return false;
    }
    if (!node_->expanded) {
      for (int side : {0, 1}) {
        views_[at(side)] = engine::observe(state_, side);
        Encoding::mask(rules.legalActions(state_, side), masks_[at(side)].data());
        queries_[at(side)] = {&views_[at(side)], side, masks_[at(side)].data()};
      }
      pending_ = 2;
      waiting_ = Waiting::expansion;
      return true;
    }

    Node &node = *node_;
    std::array<Action, 2> chosen{engine::UseMove{0}, engine::UseMove{0}};
    std::array<int, 2> keys{};
    const double sqrtVisits = std::sqrt(static_cast<double>(std::max(node.visits, 1)));
    for (int side : {0, 1}) {
      const double own = side == side_ ? node.value : -node.value;
      const bool judged = path_.empty() && side == side_ && settings.minRootVisits > 0;
      double best = -1e18;
      for (const Action &action : rules.legalActions(state_, side)) {
        const int k = keyOf(action);
        const int visits = node.n[at(side)][at(k)];
        const double q = visits ? node.w[at(side)][at(k)] / visits : own;
        double score =
            q + settings.explore * node.prior[at(side)][at(k)] * sqrtVisits / (1 + visits);
        if (judged && visits < settings.minRootVisits)
          score = 1e9 - visits; // the least tried first, until each has its minimum
        if (score > best)
          best = score, chosen[at(side)] = action, keys[at(side)] = k;
      }
    }
    std::unique_ptr<Node> &child = node.children[{keys[0], keys[1]}];
    if (child == nullptr)
      child = std::make_unique<Node>();
    rules.resolveTurn(state_, chosen[0], chosen[1], dice_);
    path_.push_back({node_, keys});
    node_ = child.get();
  }
}

void Planner::Search::resume(const Network::Output *outputs) {
  const Waiting was = waiting_;
  waiting_ = Waiting::nothing;
  pending_ = 0;
  if (was == Waiting::replacement) {
    planner_.game_.rules().resolveReplacement(state_, replacing_, pickReplacement(outputs[0]));
  } else {
    backUp(planner_.expand(*node_, masks_, outputs, side_));
    endSimulation();
  }
  advance();
}

// The simulation's result, for root, counted in every node it went through.
void Planner::Search::backUp(double value) {
  for (auto &[node, keys] : path_) {
    ++node->visits;
    for (int side : {0, 1}) {
      ++node->n[at(side)][at(keys[at(side)])];
      node->w[at(side)][at(keys[at(side)])] += side == side_ ? value : -value;
    }
  }
}

void Planner::Search::endSimulation() {
  simulating_ = false;
  if (simulation_ == 0 && planner_.settings_.noise > 0.0)
    planner_.addNoise(root_->prior[at(side_)], legal_, seed_);
  ++simulation_;
}

std::array<int, Encoding::kActions> Planner::visits(const BattleState &view, int side,
                                                    const std::vector<Action> &legal,
                                                    uint64_t seed) const {
  return analyse(view, side, legal, seed).visits;
}

Planner::Analysis Planner::analyse(const BattleState &view, int side,
                                   const std::vector<Action> &legal, uint64_t seed) const {
  Search search(*this, view, side, legal, seed);
  std::array<Network::Output, 2> outputs{};
  while (!search.done()) {
    network_.evaluate(search.pending(), outputs.data());
    search.resume(outputs.data());
  }
  return search.analysis();
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
