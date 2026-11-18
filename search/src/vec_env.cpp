#include "search/vec_env.hpp"

#include "search/encoding.hpp"

#include "engine/core/observation.hpp"
#include "engine/core/switching.hpp"

#include <algorithm>
#include <stdexcept>
#include <string>

namespace search {
namespace {
size_t at(int i) { return static_cast<size_t>(i); }

engine::Action actionOf(int32_t index) {
  if (index < engine::kMaxMovesPerPokemon)
    return engine::UseMove{index};
  return engine::SwitchAction{index - engine::kMaxMovesPerPokemon};
}
} // namespace

VecEnv::VecEnv(const Game &game, int count, uint64_t seed, int maxTurns)
    : game_(game), seed_(seed), maxTurns_(maxTurns), battles_(at(count)) {
  if (count < 1)
    throw std::invalid_argument("VecEnv: at least one battle");
  for (Battle &battle : battles_)
    start(battle);
}

void VecEnv::start(Battle &battle) {
  const uint64_t number = seed_ * 1'000'003 + started_++;
  battle.state = game_.randomBattle(number);
  battle.dice = engine::MersenneRNG(number);
  prepare(battle);
}

void VecEnv::prepare(Battle &battle) {
  battle.replacing = -1;
  const engine::BattleState &state = battle.state;
  if (state.active(0).isFainted() || state.active(1).isFainted()) {
    // One replacement at a time, the faster side first: an entry can faint on hazards.
    const int first = game_.rules().fasterSide(state, battle.dice);
    battle.replacing = state.active(first).isFainted() ? first : 1 - first;
  }
}

void VecEnv::observe(float *pokemon, int32_t *ids, float *field, float *mask) const {
  const Encoding &encoding = game_.encoding();
  const size_t pokemonSize = Encoding::kSlots * encoding.pokemonNames().size();
  const size_t idsSize = Encoding::kSlots * Encoding::kIds;
  const size_t fieldSize = encoding.fieldNames().size();
  for (size_t b = 0; b < battles_.size(); ++b) {
    const Battle &battle = battles_[b];
    for (int side : {0, 1}) {
      const size_t k = 2 * b + at(side);
      encoding.encode(engine::observe(battle.state, side), side, pokemon + k * pokemonSize,
                      ids + k * idsSize, field + k * fieldSize);
      float *options = mask + k * Encoding::kActions;
      if (battle.replacing < 0) {
        Encoding::mask(game_.rules().legalActions(battle.state, side), options);
      } else {
        std::fill(options, options + Encoding::kActions, 0.0f);
        if (side == battle.replacing)
          for (int index : engine::validSwitchTargets(battle.state, side))
            options[at(engine::kMaxMovesPerPokemon + index)] = 1.0f;
      }
    }
  }
}

void VecEnv::check(const int32_t *actions) const {
  std::vector<float> legal(Encoding::kActions);
  for (size_t b = 0; b < battles_.size(); ++b) {
    const Battle &battle = battles_[b];
    for (int side : {0, 1}) {
      if (battle.replacing >= 0 && side != battle.replacing)
        continue; // not this side's turn to choose
      const int32_t index = actions[2 * b + at(side)];
      bool ok = index >= 0 && index < Encoding::kActions;
      if (ok && battle.replacing >= 0)
        ok = index >= engine::kMaxMovesPerPokemon &&
             engine::isValidSwitchTarget(battle.state, side, index - engine::kMaxMovesPerPokemon);
      else if (ok) {
        Encoding::mask(game_.rules().legalActions(battle.state, side), legal.data());
        ok = legal[at(index)] == 1.0f;
      }
      if (!ok)
        throw std::invalid_argument("VecEnv: battle " + std::to_string(b) + ", side " +
                                    std::to_string(side) + ": option " + std::to_string(index) +
                                    " is not legal");
    }
  }
}

void VecEnv::step(const int32_t *actions, float *rewards, uint8_t *done) {
  check(actions); // every action first: an illegal one leaves every battle as it was
  const engine::BattleEngine &rules = game_.rules();
  for (size_t b = 0; b < battles_.size(); ++b) {
    Battle &battle = battles_[b];
    engine::BattleState &state = battle.state;
    if (battle.replacing >= 0) {
      const int32_t index = actions[2 * b + at(battle.replacing)];
      rules.resolveReplacement(state, battle.replacing, index - engine::kMaxMovesPerPokemon);
    } else {
      rules.resolveTurn(state, actionOf(actions[2 * b]), actionOf(actions[2 * b + 1]), battle.dice);
    }

    rewards[2 * b] = rewards[2 * b + 1] = 0.0f;
    done[b] = state.isOver() || state.turn >= maxTurns_;
    if (done[b]) {
      const bool lost0 = state.sideHasLost(0), lost1 = state.sideHasLost(1);
      if (lost0 != lost1) {
        rewards[2 * b] = lost1 ? 1.0f : -1.0f;
        rewards[2 * b + 1] = -rewards[2 * b];
      }
      ++finished_;
      start(battle);
    } else {
      prepare(battle);
    }
  }
}
} // namespace search
