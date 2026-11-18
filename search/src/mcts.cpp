#include "search/mcts.hpp"

#include "engine/core/rng.hpp"
#include "engine/core/switching.hpp"
#include "engine/ffi/ffi.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <memory>
#include <random>
#include <stdexcept>
#include <utility>
#include <variant>

namespace search {
namespace {
using engine::Action;
using engine::BattlePokemon;
using engine::BattleState;

constexpr int kDrawsPerWorld = 100; // attempts at a legal team before keeping stand-ins

size_t at(int i) { return static_cast<size_t>(i); }

// An option as a number: the same option keeps its statistics across worlds and dice.
int keyOf(const Action &action) {
  return std::holds_alternative<engine::UseMove>(action)
             ? std::get<engine::UseMove>(action).moveIndex
             : 16 + std::get<engine::SwitchAction>(action).teamIndex;
}

double endValue(const BattleState &state, int root) {
  const bool lost = state.sideHasLost(root);
  const bool won = state.sideHasLost(1 - root);
  return lost && won ? 0.5 : (won ? 1.0 : 0.0);
}
} // namespace

// One node: per side, the statistics of each of its options here; one child per pair of options.
struct Mcts::Node {
  struct Option {
    int key;
    int visits = 0;
    double value = 0.0; // summed, for the side that chose it
  };
  int visits = 0;
  std::array<std::vector<Option>, 2> options;
  std::map<std::pair<int, int>, std::unique_ptr<Node>> children;

  Option &option(int side, int key) {
    std::vector<Option> &mine = options[at(side)];
    for (Option &o : mine)
      if (o.key == key)
        return o;
    return mine.emplace_back(Option{key});
  }
};

Mcts::Mcts(const Game &game, const Forecaster &evaluator, MctsSettings settings)
    : game_(game), evaluator_(evaluator), settings_(settings) {}

std::vector<BattleState> Mcts::worlds(const BattleState &view, int foe, uint64_t seed) const {
  const engine::DataLoader &data = game_.data();
  std::vector<int> hidden, unseen;
  std::vector<bool> seen(at(data.speciesCount()), false);
  for (int i = 0; i < view.team_size[at(foe)]; ++i) {
    const BattlePokemon &p = view.teams[at(foe)][at(i)];
    if (p.isEmpty())
      hidden.push_back(i);
    else
      seen[at(p.species_id)] = true;
  }
  for (int id = 0; id < data.speciesCount(); ++id)
    if (!seen[at(id)])
      unseen.push_back(id);

  std::mt19937_64 rng(seed);
  std::vector<BattleState> found;
  for (int w = 0; w < settings_.worlds; ++w) {
    BattleState world = view;
    bool legal = false;
    for (int attempt = 0; attempt < kDrawsPerWorld && !legal; ++attempt) {
      std::shuffle(unseen.begin(), unseen.end(), rng);
      for (size_t h = 0; h < hidden.size(); ++h)
        world.teams[at(foe)][at(hidden[h])] = engine::ffi::make_combatant(unseen[h]);
      try {
        engine::ffi::validate_team(world.teams[at(foe)], world.team_size[at(foe)]);
        legal = true;
      } catch (const std::invalid_argument &) {
      }
    }
    if (!legal) // never seen so far: a copy of the active, as rules 6 and 7 do
      for (int h : hidden)
        world.teams[at(foe)][at(h)] = engine::ffi::make_combatant(view.active(foe).species_id);
    found.push_back(world);
  }
  return found;
}

void Mcts::replaceFainted(BattleState &state, engine::RNG &dice) const {
  const Estimate &estimate = game_.estimate();
  while (!state.isOver() && (state.active(0).isFainted() || state.active(1).isFainted())) {
    // One at a time, the faster side first, as in a battle: an entry can faint on hazards.
    const int first = game_.rules().fasterSide(state, dice);
    const int side = state.active(first).isFainted() ? first : 1 - first;
    const BattlePokemon &foe = state.active(1 - side);
    int best = -1;
    double bestMatchup = 0.0;
    for (int index : engine::validSwitchTargets(state, side)) {
      const BattlePokemon &candidate = state.teams[at(side)][at(index)];
      const double matchup = estimate.bestPower(candidate, foe.species_id) -
                             estimate.bestPower(foe, candidate.species_id);
      if (best < 0 || matchup > bestMatchup)
        best = index, bestMatchup = matchup;
    }
    game_.rules().resolveReplacement(state, side, best);
  }
}

double Mcts::rate(BattleState &state, int root) const {
  if (state.isOver())
    return endValue(state, root);
  return evaluator_.odds(state, root);
}

double Mcts::simulate(BattleState &state, Node &node, int depth, int root,
                      engine::RNG &dice) const {
  replaceFainted(state, dice);
  if (state.isOver() || depth == settings_.depth)
    return rate(state, root);

  // Each side picks its own option, by UCB1 on its own statistics at this node.
  std::array<Action, 2> chosen{engine::UseMove{0}, engine::UseMove{0}};
  std::array<int, 2> keys{};
  for (int side : {0, 1}) {
    const std::vector<Action> legal = game_.rules().legalActions(state, side);
    double best = -1.0;
    for (const Action &action : legal) {
      const Node::Option &o = node.option(side, keyOf(action));
      const double score =
          o.visits == 0 ? 1e9 // every option once, in order
                        : o.value / o.visits +
                              settings_.explore * std::sqrt(std::log(node.visits) / o.visits);
      if (score > best)
        best = score, chosen[at(side)] = action, keys[at(side)] = o.key;
    }
  }

  std::unique_ptr<Node> &child = node.children[{keys[0], keys[1]}];
  const bool fresh = child == nullptr;
  if (fresh)
    child = std::make_unique<Node>();
  game_.rules().resolveTurn(state, chosen[0], chosen[1], dice);
  // A new node is rated at once: the model stands in for the rest of the battle.
  const double value = fresh ? (replaceFainted(state, dice), rate(state, root))
                             : simulate(state, *child, depth + 1, root, dice);

  ++node.visits;
  for (int side : {0, 1}) {
    Node::Option &o = node.option(side, keys[at(side)]);
    ++o.visits;
    o.value += side == root ? value : 1.0 - value;
  }
  return value;
}

int Mcts::chooseAction(const BattleState &view, int side, const std::vector<Action> &legal,
                       uint64_t seed) const {
  if (legal.size() == 1)
    return 0;
  const std::vector<BattleState> guesses = worlds(view, 1 - side, seed);
  Node root;
  for (int i = 0; i < settings_.iterations; ++i) {
    BattleState state = guesses[at(i % settings_.worlds)];
    engine::MersenneRNG dice(seed * 1'000'003 + static_cast<uint64_t>(i));
    simulate(state, root, 0, side, dice);
  }
  int best = 0, bestVisits = -1;
  for (size_t i = 0; i < legal.size(); ++i) {
    const int visits = root.option(side, keyOf(legal[i])).visits;
    if (visits > bestVisits)
      best = static_cast<int>(i), bestVisits = visits;
  }
  return best;
}
} // namespace search
