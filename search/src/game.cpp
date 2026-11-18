#include "search/game.hpp"

#include "engine/core/rng.hpp"
#include "engine/core/switching.hpp"
#include "engine/ffi/ffi.hpp"

#include <algorithm>
#include <random>
#include <stdexcept>
#include <vector>

namespace search {
namespace {
constexpr int kTeam = engine::kTeamSize;

// Draws teams until the engine accepts one: about one draw in two breaks the Mega or the legendary
// limit.
std::array<engine::BattlePokemon, kTeam> randomTeam(std::mt19937_64 &rng) {
  std::vector<int> species(static_cast<size_t>(engine::ffi::species_count()));
  for (size_t i = 0; i < species.size(); ++i)
    species[i] = static_cast<int>(i);
  while (true) {
    std::shuffle(species.begin(), species.end(), rng);
    std::array<engine::BattlePokemon, kTeam> team{};
    for (size_t i = 0; i < kTeam; ++i)
      team[i] = engine::ffi::make_combatant(species[i]);
    try {
      engine::ffi::validate_team(team, kTeam);
      return team;
    } catch (const std::invalid_argument &) {
    }
  }
}

engine::DataLoader load(const std::string &dataDir) {
  engine::ffi::engine_init(dataDir); // the flat API sets battles up: same catalog
  engine::DataLoader data;
  data.loadAll(dataDir);
  return data;
}

template <class T> const T &pick(const std::vector<T> &options, std::mt19937_64 &rng) {
  std::uniform_int_distribution<size_t> index(0, options.size() - 1);
  return options[index(rng)];
}
} // namespace

Game::Game(const std::string &dataDir) : data_(load(dataDir)) {}

engine::BattleState Game::randomBattle(uint64_t seed) const {
  std::mt19937_64 rng(seed);
  engine::BattleState state;
  for (size_t side = 0; side < 2; ++side) {
    state.teams[side] = randomTeam(rng);
    state.team_size[side] = kTeam;
  }
  engine::ffi::start_battle(state, seed);
  return state;
}

Outcome playRandomly(const Game &game, engine::BattleState &state, uint64_t seed, int maxTurns) {
  const engine::BattleEngine &rules = game.rules();
  engine::MersenneRNG dice(seed);
  std::mt19937_64 choices(seed ^ 0x9E3779B97F4A7C15ULL);
  while (!state.isOver() && state.turn < maxTurns) {
    if (state.active(0).isFainted() || state.active(1).isFainted()) {
      // One replacement at a time, the faster side first: an incoming Pokemon can faint on entry.
      const int first = rules.fasterSide(state, dice);
      const int side = state.active(first).isFainted() ? first : 1 - first;
      rules.resolveReplacement(state, side, pick(engine::validSwitchTargets(state, side), choices));
      continue;
    }
    const engine::Action a0 = pick(rules.legalActions(state, 0), choices);
    const engine::Action a1 = pick(rules.legalActions(state, 1), choices);
    rules.resolveTurn(state, a0, a1, dice);
  }
  int winner = -1;
  if (state.sideHasLost(1) && !state.sideHasLost(0))
    winner = 0;
  else if (state.sideHasLost(0) && !state.sideHasLost(1))
    winner = 1;
  return {winner, state.turn};
}

std::vector<engine::BattleState> openings(const Game &game, int count, int turns) {
  std::vector<engine::BattleState> found;
  for (uint64_t seed = 0; static_cast<int>(found.size()) < count; ++seed) {
    engine::BattleState state = game.randomBattle(seed);
    playRandomly(game, state, seed, turns);
    if (!state.isOver() && !state.active(0).isFainted() && !state.active(1).isFainted())
      found.push_back(state);
  }
  return found;
}
} // namespace search
