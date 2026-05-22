#pragma once

#include "engine/move.hpp"
#include "engine/pokemon.hpp"
#include "engine/types.hpp"

#include <string>
#include <unordered_map>

namespace engine {

// Owns all game content (types, moves, species). Lives for the program's lifetime.
class DataLoader {
public:
    // Loads data/types.json, data/moves/*.json, data/pokemon/*.json.
    // Throws on invalid file or missing reference.
    void loadAll(const std::string& dataDir);

    const TypeChart& typeChart() const { return typeChart_; }
    const Move& move(const std::string& name) const;
    const Species& species(const std::string& id) const;

    bool hasMove(const std::string& name) const { return moves_.count(name) > 0; }
    bool hasSpecies(const std::string& id) const { return species_.count(id) > 0; }

private:
    void loadTypes(const std::string& path);
    void loadMoves(const std::string& dir);
    void loadSpecies(const std::string& dir);

    TypeChart typeChart_;
    std::unordered_map<std::string, Move> moves_;
    std::unordered_map<std::string, Species> species_;
};

} // namespace engine
