#pragma once

#include "engine/stats.hpp"
#include "engine/types.hpp"

#include <array>
#include <string>
#include <vector>

namespace engine {

constexpr int kMaxMovesPerPokemon = 4;
constexpr int kNoMove = -1;
constexpr int kNoSpecies = -1;

struct Species {
    std::string id;
    std::string displayName;
    Stats baseStats;
    Type type1 = Type::Normal;
    Type type2 = Type::Normal;
    std::string ability;
    std::vector<std::string> movepool;

    bool isDualType() const { return type1 != type2; }
};

// FFI contract: POD only, no pointers, no std::vector, no std::string.
// All references to game content are integer indices into DataLoader catalogs.
struct BattlePokemon {
    int species_id = kNoSpecies;
    int level = 50;
    Stats stats;
    int currentHp = 0;
    std::array<int, kMaxMovesPerPokemon> move_ids { kNoMove, kNoMove, kNoMove, kNoMove };

    bool isFainted() const { return currentHp <= 0; }
    bool isEmpty() const { return species_id == kNoSpecies; }
};

} // namespace engine
