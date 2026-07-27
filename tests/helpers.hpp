#pragma once

#include "engine/core/data_loader.hpp"
#include "engine/model/pokemon.hpp"
#include "engine/model/stats.hpp"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

namespace engine::test {

inline BattlePokemon buildCombatant(const DataLoader &data, const std::string &speciesName,
                                    int level, const std::vector<std::string> &moveNames) {
  BattlePokemon p;
  p.species_id = data.findSpeciesId(speciesName);
  REQUIRE(p.species_id >= 0);

  const Species &sp = data.speciesByIndex(p.species_id);
  p.level = level;
  p.stats = computeSpeciesStats(sp, level);
  p.currentHp = p.stats.hp;
  p.item_id = sp.item.empty() ? kNoItem : data.findItemId(sp.item);

  for (size_t i = 0; i < moveNames.size() && i < kMaxMovesPerPokemon; ++i) {
    int mid = data.findMoveId(moveNames[i]);
    REQUIRE(mid >= 0);
    p.move_ids[i] = mid;
    p.pp[i] = data.moveByIndex(mid).pp;
  }
  return p;
}

// Test-only: swap a species' ability inside THIS loader instance (each
// test case owns its own DataLoader, so nothing leaks across tests). The
// const_cast is confined here; production code never mutates the catalog.
inline void overrideAbility(DataLoader &data, const std::string &speciesId,
                            const std::string &ability) {
  const_cast<Species &>(data.speciesByIndex(data.findSpeciesId(speciesId))).ability = ability;
}

} // namespace engine::test
