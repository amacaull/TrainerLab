#include "search/estimate.hpp"

#include "engine/model/move.hpp"

#include <algorithm>

namespace search {
namespace {
using engine::BattlePokemon;

constexpr double kLevelFactor = 2 * 100 / 5.0 + 2; // every Pokemon is level 100
constexpr double kAverageRoll = 0.925;
constexpr double kStabBonus = 1.5;

size_t at(int i) { return static_cast<size_t>(i); }

// As in Python: an int product, then a true division.
double staged(int value, const BattlePokemon &pokemon, engine::StatIndex stat) {
  const int stage = pokemon.stat_stages[at(static_cast<int>(stat))];
  return stage >= 0 ? static_cast<double>(value * (2 + stage)) / 2
                    : static_cast<double>(value * 2) / (2 - stage);
}
} // namespace

Estimate::Estimate(const engine::DataLoader &data)
    : data_(data), choiceBand_(data.findItemId("ChoiceBand")),
      choiceSpecs_(data.findItemId("ChoiceSpecs")), lifeOrb_(data.findItemId("LifeOrb")),
      choiceScarf_(data.findItemId("ChoiceScarf")) {}

int Estimate::heldItem(const BattlePokemon &pokemon) const {
  return pokemon.item_consumed ? engine::kNoItem : pokemon.item_id;
}

double Estimate::typeFactor(int moveId, int defenderSpecies) const {
  const engine::Species &sp = data_.speciesByIndex(defenderSpecies);
  return data_.typeChart().effectiveness(data_.moveByIndex(moveId).type, sp.type1, sp.type2);
}

bool Estimate::sameType(int moveId, int speciesId) const {
  const engine::Type type = data_.moveByIndex(moveId).type;
  const engine::Species &sp = data_.speciesByIndex(speciesId);
  return type == sp.type1 || type == sp.type2;
}

double Estimate::damage(const BattlePokemon &attacker, const BattlePokemon &defender,
                        int slot) const {
  const int moveId = attacker.move_ids[at(slot)];
  const engine::Move &move = data_.moveByIndex(moveId);
  if (move.power == 0)
    return 0.0;
  const bool physical = move.category == engine::MoveCategory::Physical;
  const double attack = physical ? staged(attacker.stats.atk, attacker, engine::StatIndex::Atk)
                                 : staged(attacker.stats.specAtk, attacker, engine::StatIndex::SpA);
  const double defense = physical
                             ? staged(defender.stats.def, defender, engine::StatIndex::Def)
                             : staged(defender.stats.specDef, defender, engine::StatIndex::SpD);
  const double base = kLevelFactor * move.power * attack / defense / 50 + 2;
  const double stab = sameType(moveId, attacker.species_id) ? kStabBonus : 1.0;
  const double effectiveness = typeFactor(moveId, defender.species_id);
  const int item = heldItem(attacker);
  double boost = 1.0;
  if (item != engine::kNoItem) {
    if (item == lifeOrb_)
      boost = 1.3;
    else if (item == (physical ? choiceBand_ : choiceSpecs_))
      boost = 1.5;
  }
  return base * stab * effectiveness * boost * kAverageRoll;
}

double Estimate::strongestHit(const BattlePokemon &attacker, const BattlePokemon &defender) const {
  double best = 0.0;
  for (int slot = 0; slot < engine::kMaxMovesPerPokemon; ++slot) {
    const double hit = damage(attacker, defender, slot);
    best = slot == 0 ? hit : std::max(best, hit);
  }
  return best;
}

double Estimate::effectiveSpeed(const BattlePokemon &pokemon) const {
  double speed = staged(pokemon.stats.speed, pokemon, engine::StatIndex::Spe);
  const int item = heldItem(pokemon);
  if (item != engine::kNoItem && item == choiceScarf_)
    speed *= 1.5;
  return pokemon.status == engine::Status::Paralysis ? speed / 2 : speed;
}

double Estimate::powerScore(const BattlePokemon &attacker, int slot, int defenderSpecies) const {
  const int moveId = attacker.move_ids[at(slot)];
  const engine::Move &move = data_.moveByIndex(moveId);
  const double stab = sameType(moveId, attacker.species_id) ? kStabBonus : 1.0;
  const double effectiveness = typeFactor(moveId, defenderSpecies);
  const double hitChance = move.accuracy > 0 ? move.accuracy / 100.0 : 1.0;
  return move.power * stab * effectiveness * hitChance;
}

int Estimate::strongestSlot(const BattlePokemon &attacker, int defenderSpecies) const {
  int best = 0;
  double bestScore = powerScore(attacker, 0, defenderSpecies);
  for (int slot = 1; slot < engine::kMaxMovesPerPokemon; ++slot) {
    const double score = powerScore(attacker, slot, defenderSpecies);
    if (score > bestScore) // the first of equal scores, as Python's max
      best = slot, bestScore = score;
  }
  return best;
}

double Estimate::bestPower(const BattlePokemon &attacker, int defenderSpecies) const {
  return powerScore(attacker, strongestSlot(attacker, defenderSpecies), defenderSpecies);
}
} // namespace search
