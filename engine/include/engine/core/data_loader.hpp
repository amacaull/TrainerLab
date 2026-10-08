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
EffectPtr makeEffectFromJson(const nlohmann::json &j);

class DataLoader {
public:
  void loadAll(const std::string &dataDir);

  // Test fixtures only: the neutral moves the unit tests need (Tackle, Growl) stay out of the
  // shipped catalog.
  void loadExtraContent(const std::string &dataDir);

  const TypeChart &typeChart() const { return typeChart_; }

  const Move &moveByIndex(int id) const;
  const Species &speciesByIndex(int id) const;

  // -1 on a miss, never throws: probing is legitimate for FFI callers.
  int findMoveId(const std::string &name) const;
  int findSpeciesId(const std::string &id) const;
  int findItemId(const std::string &name) const;
  int findAbilityId(const std::string &name) const;

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
