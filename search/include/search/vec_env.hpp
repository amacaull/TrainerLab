#pragma once

#include "search/game.hpp"

#include "engine/core/battle_state.hpp"
#include "engine/core/rng.hpp"

#include <cstdint>
#include <vector>

namespace search {
// Many battles at once, for self-play: both sides of every battle are played by the caller.
//
// At each step, every battle waits for a decision. On a turn, both sides choose among their legal
// options; when a fainted Pokemon must be replaced, only that side chooses, among its switches
// (the other side's mask is all 0 and its action is ignored). A battle that ends (or reaches the
// turn limit, a draw) gives +1 to the winner, -1 to the loser, 0 each on a draw, and starts again
// at once with new teams: the arrays always describe battles waiting for a decision.
//
// Options are numbered as in Encoding: moves 0-3 by slot, switches 4-9 by team index. The seed
// fixes every team and every roll: the same seed and the same actions give the same battles.
class VecEnv {
public:
  VecEnv(const Game &game, int count, uint64_t seed, int maxTurns = 1000);

  int count() const { return static_cast<int>(battles_.size()); }

  // Writes, for every battle and both sides (battle-major, then side): the encoding of what that
  // side sees, and the mask of its options.
  void observe(float *pokemon, int32_t *ids, float *field, float *mask) const;

  // Plays one decision in every battle: actions[2 * b + side]. Writes, per battle and side, the
  // reward, and per battle whether it ended (and started again). An illegal option throws, before
  // any battle moves.
  void step(const int32_t *actions, float *rewards, uint8_t *done);

  // Battles finished since the start.
  int64_t finished() const { return finished_; }

private:
  struct Battle {
    engine::BattleState state;
    engine::MersenneRNG dice{0};
    int replacing = -1; // the side that must replace, or -1 for a turn
  };

  void check(const int32_t *actions) const;
  void start(Battle &battle);
  void prepare(Battle &battle); // sets who decides next

  const Game &game_;
  uint64_t seed_;
  int maxTurns_;
  uint64_t started_ = 0;
  int64_t finished_ = 0;
  std::vector<Battle> battles_;
};
} // namespace search
