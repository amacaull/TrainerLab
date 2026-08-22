#include "engine/ffi/ffi.hpp"

#include "engine/abilities/ability.hpp"
#include "engine/core/data_loader.hpp"
#include "engine/items/item.hpp"
#include "engine/model/move.hpp"
#include "engine/model/pokemon.hpp"

#include <stdexcept>
#include <string>

namespace engine::ffi {

namespace {

// D7: the DataLoader outlives the calls, is read-only once loaded, and the
// engine is stateless - so N battles can run concurrently on distinct
// BattleStates without a lock.
DataLoader &loaderStorage() {
  static DataLoader loader;
  return loader;
}

std::string &dataDirStorage() {
  static std::string dir;
  return dir;
}

const DataLoader &loader() {
  if (dataDirStorage().empty())
    throw std::runtime_error("E_INIT: engine_init has not been called");
  return loaderStorage();
}

void requireId(bool ok, const char *what, int id, int count) {
  if (!ok)
    throw std::out_of_range("E_ARG: " + std::string(what) + " id " + std::to_string(id) +
                            " out of range [0, " + std::to_string(count) + ")");
}

} // namespace

void engine_init(const std::string &dataDir) {
  std::string &current = dataDirStorage();
  if (!current.empty()) {
    if (current == dataDir)
      return; // idempotent
    throw std::runtime_error("E_INIT: engine already initialised with '" + current +
                             "', refusing to re-initialise with '" + dataDir +
                             "' (two catalogs would mean two id spaces)");
  }

  try {
    loaderStorage().loadAll(dataDir);
  } catch (const std::exception &e) {
    // The loader is left half-populated; keep the process uninitialised so a
    // retry starts clean rather than serving a partial catalog.
    loaderStorage() = DataLoader{};
    throw std::runtime_error("E_DATA: " + std::string(e.what()));
  }
  current = dataDir;
}

bool engine_is_initialised() { return !dataDirStorage().empty(); }

std::string engine_data_dir() { return dataDirStorage(); }

int species_count() { return loader().speciesCount(); }
int move_count() { return loader().moveCount(); }
int item_count() { return engine::itemCount(); }
int ability_count() { return engine::abilityCount(); }

int find_species_id(const std::string &id_string) { return loader().findSpeciesId(id_string); }
int find_move_id(const std::string &name) { return loader().findMoveId(name); }
int find_item_id(const std::string &name) { return loader().findItemId(name); }
int find_ability_id(const std::string &name) { return loader().findAbilityId(name); }

std::string species_id_string(int id) {
  const DataLoader &d = loader();
  requireId(d.isValidSpeciesId(id), "species", id, d.speciesCount());
  return d.speciesByIndex(id).id;
}

std::string species_display_name(int id) {
  const DataLoader &d = loader();
  requireId(d.isValidSpeciesId(id), "species", id, d.speciesCount());
  return d.speciesByIndex(id).displayName;
}

std::string move_name(int id) {
  const DataLoader &d = loader();
  requireId(d.isValidMoveId(id), "move", id, d.moveCount());
  return d.moveByIndex(id).name;
}

std::string item_name(int id) {
  const Item *item = engine::itemByIndex(id);
  requireId(item != nullptr, "item", id, engine::itemCount());
  return item->name();
}

std::string ability_name(int id) {
  const Ability *ability = engine::abilityByIndex(id);
  requireId(ability != nullptr, "ability", id, engine::abilityCount());
  return ability->name();
}

} // namespace engine::ffi
