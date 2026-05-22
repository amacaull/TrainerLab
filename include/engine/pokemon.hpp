#pragma once

#include "engine/stats.hpp"
#include "engine/types.hpp"

#include <string>
#include <vector>

namespace engine {

// Static species data, shared by all instances (all Charizard share one).
struct Species {
    std::string id;
    std::string displayName;
    Stats baseStats;
    Type type1 = Type::Normal;
    Type type2 = Type::Normal; // equal to type1 if mono-type
    std::string ability;
    std::vector<std::string> movepool;

    bool isDualType() const { return type1 != type2; }
};

// Instance in battle. Species lives in the DataLoader for the whole match.
struct BattlePokemon {
    const Species* species = nullptr;
    int level = 50;
    Stats stats;
    int currentHp = 0;
    std::vector<std::string> moves; // chosen move names, up to 4

    bool isFainted() const { return currentHp <= 0; }
};

} // namespace engine
