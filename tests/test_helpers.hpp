#pragma once

#include "engine/data_loader.hpp"
#include "engine/pokemon.hpp"
#include "engine/stats.hpp"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

namespace engine::test {

inline BattlePokemon buildCombatant(const DataLoader& data,
                                    const std::string& speciesName,
                                    int level,
                                    const std::vector<std::string>& moveNames) {
    BattlePokemon p;
    p.species_id = data.findSpeciesId(speciesName);
    REQUIRE(p.species_id >= 0);

    const Species& sp = data.speciesByIndex(p.species_id);
    p.level = level;
    p.stats = computeStats(sp.baseStats, level);
    p.currentHp = p.stats.hp;

    for (size_t i = 0; i < moveNames.size() && i < kMaxMovesPerPokemon; ++i) {
        int mid = data.findMoveId(moveNames[i]);
        REQUIRE(mid >= 0);
        p.move_ids[i] = mid;
    }
    return p;
}

} // namespace engine::test
