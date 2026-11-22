#pragma once

#include "search/game.hpp"
#include "search/network.hpp"

#include "engine/core/battle_state.hpp"
#include "engine/core/rng.hpp"

#include <array>
#include <cstdint>
#include <memory>
#include <span>
#include <utility>
#include <vector>

namespace search {
// Fixed before any measurement.
struct PlannerSettings {
  int simulations = 200; // per decision
  int worlds = 8;        // guesses at the foe's unseen Pokemon, used in turn
  double explore = 1.5;  // the weight of the prior in PUCT
  // Self-play only (rule 11 plays with 0): the share of the root's prior replaced by noise, a flat
  // Dirichlet draw over the options, so that the search also tries what the network neglects.
  double noise = 0.0;
  // A judge's setting (0 to play): every legal option of the side searched is tried at least
  // this many times at the root, so that even an option the network dislikes gets a value.
  int minRootVisits = 0;
};

// Rule 11: a tree search over simultaneous moves guided by the policy and value network, as in
// AlphaZero.
//
// The tree is rule 9's: each node keeps, for each side, the statistics of its own options, and
// the pair of choices leads to the child; the foe's unseen Pokemon are guessed as legal teams.
// What changes is the network. A node is expanded at its first visit: the network gives each
// side's policy over its options (the priors) and the value of the position, which is backed up
// at once, without playing further. Each side then picks by PUCT:
//
//   Q(a) + explore x P(a) x sqrt(N) / (1 + n(a))
//
// an option never tried starting from the node's value. A fainted Pokemon is replaced by the
// network's favourite switch. The option played is the one the search visited most.
class Planner {
public:
  Planner(const Game &game, const Network &network, PlannerSettings settings = {});

  // The index, in `legal`, of the option played. The seed fixes the worlds and the dice.
  int chooseAction(const engine::BattleState &view, int side,
                   const std::vector<engine::Action> &legal, uint64_t seed) const;

  // How many times the search visited each of `side`'s ten options (Encoding's numbering) at the
  // root: what self-play learns from. With one legal option, no search: all on that option.
  std::array<int, Encoding::kActions> visits(const engine::BattleState &view, int side,
                                             const std::vector<engine::Action> &legal,
                                             uint64_t seed) const;

  // At the root, per option of `side` (Encoding's numbering): its visits, and the mean result
  // for `side` of the simulations that tried it (0 if none did).
  struct Analysis {
    std::array<int, Encoding::kActions> visits{};
    std::array<double, Encoding::kActions> value{};
  };
  Analysis analyse(const engine::BattleState &view, int side,
                   const std::vector<engine::Action> &legal, uint64_t seed) const;

  // The team index of the network's favourite Pokemon to send in, without search.
  int chooseReplacement(const engine::BattleState &view, int side) const;
  // Palier 10.4: the team index, among `options`, whose replacement leaves `side` the best value:
  // each option played on the view, then searched `simulations` simulations (the network's value
  // when the side has one option or the battle is over). The seed fixes the searches.
  int searchReplacement(const engine::BattleState &view, int side, const std::vector<int> &options,
                        uint64_t seed, int simulations) const;
  // The same in two steps, for a caller that batches its queries: the mask of `side`'s switches
  // in `view`, then the team index the network's output for it picks.
  static std::array<float, Encoding::kActions> replacementMask(const engine::BattleState &view,
                                                               int side);
  static int pickReplacement(const Network::Output &out);

  class Search;

private:
  struct Node;

  double expand(Node &node, const std::array<std::array<float, Encoding::kActions>, 2> &masks,
                const Network::Output *outputs, int root) const;
  void addNoise(std::array<double, Encoding::kActions> &prior,
                const std::vector<engine::Action> &legal, uint64_t seed) const;

  const Game &game_;
  const Network &network_;
  PlannerSettings settings_;
};

// One search (analyse's), step by step, so that self-play can batch the network's queries across
// battles (palier 8.3). pending() is what the search waits on the network for, resume() gives it
// the outputs, in order; once done(), analysis() is its result. The queries point into the
// search: it stays where it is built.
class Planner::Search {
public:
  Search(const Planner &planner, const engine::BattleState &view, int side,
         const std::vector<engine::Action> &legal, uint64_t seed);
  ~Search();
  Search(const Search &) = delete;
  Search &operator=(const Search &) = delete;

  bool done() const { return done_; }
  std::span<const Network::Query> pending() const { return {queries_.data(), pending_}; }
  void resume(const Network::Output *outputs);
  const Analysis &analysis() const { return analysis_; }

private:
  enum class Waiting { nothing, replacement, expansion };

  void advance();
  bool descend(); // true: waiting on the network
  void backUp(double value);
  void endSimulation();

  const Planner &planner_;
  int side_;
  std::vector<engine::Action> legal_;
  uint64_t seed_;
  std::vector<engine::BattleState> guesses_;
  std::unique_ptr<Node> root_;
  int simulation_ = 0;
  bool simulating_ = false, done_ = false;
  Analysis analysis_;

  // The simulation under way: its state, dice, and the nodes it went through with the options.
  engine::BattleState state_;
  engine::MersenneRNG dice_{0};
  Node *node_ = nullptr;
  std::vector<std::pair<Node *, std::array<int, 2>>> path_;

  Waiting waiting_ = Waiting::nothing;
  int replacing_ = 0;
  std::array<engine::BattleState, 2> views_{};
  std::array<std::array<float, Encoding::kActions>, 2> masks_{};
  std::array<Network::Query, 2> queries_{};
  size_t pending_ = 0;
};
} // namespace search
