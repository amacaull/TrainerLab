#pragma once

#include "engine/core/battle_state.hpp"
#include "engine/core/data_loader.hpp"
#include "engine/core/engine.hpp"

#include <cstdint>
#include <string>

namespace search {
// The rules and the catalog, loaded once, for the search to play on.
//
// The search calls the engine's own C++ API, not the flat one: the flat API validates and copies
// the state at every call, which is right at a language boundary and too slow for millions of
// simulated turns. The flat API still sets battles up, so that a battle starts exactly as in
// Python.
class Game {
public:
  explicit Game(const std::string &dataDir);

  const engine::BattleEngine &rules() const { return rules_; }

  // Two random legal teams of six, the same ones for the same seed, started with this seed.
  engine::BattleState randomBattle(uint64_t seed) const;

private:
  engine::DataLoader data_;
  engine::BattleEngine rules_{data_};
};

// How a battle played to its end came out.
struct Outcome {
  int winner; // 0 or 1; -1 for a draw, or a battle stopped at the turn limit
  int turns;
};

// Plays `state` on, every choice uniformly random among the legal ones, until the battle is over or
// its turn reaches `maxTurns`: the playout of a Monte Carlo search, and the unit of the benchmark.
// The same seed plays the same battle.
Outcome playRandomly(const Game &game, engine::BattleState &state, uint64_t seed,
                     int maxTurns = 1000);
} // namespace search
