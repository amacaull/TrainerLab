#pragma once

#include "engine/core/data_loader.hpp"
#include "engine/model/pokemon.hpp"
#include "engine/model/stats.hpp"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

namespace engine::test {

// Loads the shipped roster plus the neutral instruments the unit tests need
// (Tackle, Growl, Protect...). The shipped catalog stays exactly the 49
// species and 95 moves of the team sheet (ADR #49).
inline void loadAll(DataLoader &data) {
  data.loadAll(BATTLE_ENGINE_DATA_DIR);
  data.loadExtraContent(BATTLE_ENGINE_FIXTURE_DIR);
}

inline BattlePokemon buildCombatant(const DataLoader &data, const std::string &speciesName,
                                    const std::vector<std::string> &moveNames) {
  BattlePokemon p;
  p.species_id = data.findSpeciesId(speciesName);
  REQUIRE(p.species_id >= 0);

  const Species &sp = data.speciesByIndex(p.species_id);
  p.level = kBattleLevel;
  p.stats = computeSpeciesStats(sp, kBattleLevel);
  p.currentHp = p.stats.hp;
  // Bare fixture: unit tests inject items explicitly. buildLoadout equips
  // the species' real held item for integration scenarios.
  p.item_id = kNoItem;

  for (size_t i = 0; i < moveNames.size() && i < kMaxMovesPerPokemon; ++i) {
    int mid = data.findMoveId(moveNames[i]);
    REQUIRE(mid >= 0);
    p.move_ids[i] = mid;
    p.pp[i] = data.moveByIndex(mid).pp;
  }
  return p;
}

inline BattlePokemon buildLoadout(const DataLoader &data, const std::string &speciesId,
                                  const std::vector<std::string> &moves) {
  BattlePokemon p = buildCombatant(data, speciesId, moves);
  const Species &sp = data.speciesByIndex(p.species_id);
  p.item_id = sp.item.empty() ? kNoItem : data.findItemId(sp.item);
  return p;
}

// Test-only: swap a species' ability inside THIS loader instance (each
// test case owns its own DataLoader, so nothing leaks across tests). The
// const_cast is confined here; production code never mutates the catalog.
inline void overrideAbility(DataLoader &data, const std::string &speciesId,
                            const std::string &ability) {
  const_cast<Species &>(data.speciesByIndex(data.findSpeciesId(speciesId))).ability = ability;
}

// Isolates one catalog field at a time: the weight tiers and the team rules
// are checked without hunting for a species that happens to fit.
inline void overrideWeight(DataLoader &data, const std::string &speciesId, double kg) {
  const_cast<Species &>(data.speciesByIndex(data.findSpeciesId(speciesId))).weightKg = kg;
}

inline void overrideLegendary(DataLoader &data, const std::string &speciesId, bool legendary) {
  const_cast<Species &>(data.speciesByIndex(data.findSpeciesId(speciesId))).legendary = legendary;
}

} // namespace engine::test
