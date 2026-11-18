#pragma once

#include "engine/core/data_loader.hpp"
#include "engine/model/pokemon.hpp"

namespace search {
// The bots' mental model of damage and speed: the C++ twin of trainerlab.game.estimate, value for
// value, and of the move score of rules 1 to 5 (trainerlab.bots.rules). A belief, not the rules:
// every input is what a player can see.
class Estimate {
public:
  explicit Estimate(const engine::DataLoader &data);

  // The HP the move in `slot` should take from `defender`, if it hits.
  double damage(const engine::BattlePokemon &attacker, const engine::BattlePokemon &defender,
                int slot) const;
  // The largest damage estimate among the attacker's moves.
  double strongestHit(const engine::BattlePokemon &attacker,
                      const engine::BattlePokemon &defender) const;
  // Speed after its stage, a Choice Scarf and paralysis.
  double effectiveSpeed(const engine::BattlePokemon &pokemon) const;

  // Rules 1 to 4's move score: power x STAB x type effectiveness x accuracy.
  double powerScore(const engine::BattlePokemon &attacker, int slot, int defenderSpecies) const;
  int strongestSlot(const engine::BattlePokemon &attacker, int defenderSpecies) const;
  double bestPower(const engine::BattlePokemon &attacker, int defenderSpecies) const;

private:
  double typeFactor(int moveId, int defenderSpecies) const;
  bool sameType(int moveId, int speciesId) const;
  int heldItem(const engine::BattlePokemon &pokemon) const;

  const engine::DataLoader &data_;
  int choiceBand_, choiceSpecs_, lifeOrb_, choiceScarf_;
};
} // namespace search
