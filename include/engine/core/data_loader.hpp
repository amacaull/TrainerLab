#pragma once

#include "engine/effects/effect.hpp"
#include "engine/model/move.hpp"
#include "engine/model/pokemon.hpp"
#include "engine/model/types.hpp"

#include <nlohmann/json_fwd.hpp>

#include <string>
#include <unordered_map>
#include <vector>

namespace engine {

// Factory for the effects declared in JSON ({"kind": "Damage", ...}).
// Add a case in data_loader.cpp when introducing a new effect kind.
EffectPtr makeEffectFromJson(const nlohmann::json &j);

// Indexed catalogs (ADR #12). Lookup tables are used only at JSON load time
// and to expose name->id for the FFI client (Rust caches ids at startup,
// then only uses integers at the boundary).
class DataLoader {
public:
  void loadAll(const std::string &dataDir);

  // Appends a second catalog on top of the shipped one, without touching the
  // type chart. Exists for the test fixtures: the roster's 95 moves are the
  // game content, and the neutral instruments the unit tests need (Tackle,
  // Growl...) must not leak into the indices frozen for the FFI (ADR #49).
  void loadExtraContent(const std::string &dataDir);

  const TypeChart &typeChart() const { return typeChart_; }

  const Move &moveByIndex(int id) const;
  const Species &speciesByIndex(int id) const;

  // Name lookup. Returns -1 on miss (no throw, safe for FFI probing).
  int findMoveId(const std::string &name) const;
  int findSpeciesId(const std::string &id) const;
  int findItemId(const std::string &name) const;    // facade over the code registry (ADR #45)
  int findAbilityId(const std::string &name) const; // same, for the ability table

  int moveCount() const { return static_cast<int>(moves_.size()); }
  int speciesCount() const { return static_cast<int>(species_.size()); }

  bool isValidMoveId(int id) const { return id >= 0 && id < moveCount(); }
  bool isValidSpeciesId(int id) const { return id >= 0 && id < speciesCount(); }
  bool isValidItemId(int id) const;
  bool isValidAbilityId(int id) const;

private:
  void loadTypes(const std::string &path);
  void loadMoves(const std::string &dir);
  void loadSpecies(const std::string &dir);

  TypeChart typeChart_;

  std::vector<Move> moves_;
  std::unordered_map<std::string, int> move_name_to_id_;

  std::vector<Species> species_;
  std::unordered_map<std::string, int> species_id_to_index_;
};

} // namespace engine
