#pragma once

#include "engine/core/data_loader.hpp"
#include "engine/model/pokemon.hpp"
#include "engine/model/stats.hpp"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

namespace engine::test {
// The shipped roster plus the neutral test instruments (Tackle, Growl, Protect...), which stay out
// of the shipped catalog.
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

// Test-only: each test case owns its DataLoader, so nothing leaks. The const_cast is confined here.
inline void overrideAbility(DataLoader &data, const std::string &speciesId,
                            const std::string &ability) {
  const_cast<Species &>(data.speciesByIndex(data.findSpeciesId(speciesId))).ability = ability;
}

inline void overrideWeight(DataLoader &data, const std::string &speciesId, double kg) {
  const_cast<Species &>(data.speciesByIndex(data.findSpeciesId(speciesId))).weightKg = kg;
}

inline void overrideLegendary(DataLoader &data, const std::string &speciesId, bool legendary) {
  const_cast<Species &>(data.speciesByIndex(data.findSpeciesId(speciesId))).legendary = legendary;
}
} // namespace engine::test
