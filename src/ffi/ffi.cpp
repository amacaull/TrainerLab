#include "engine/ffi/ffi.hpp"

#include "engine/abilities/ability.hpp"
#include "engine/core/data_loader.hpp"
#include "engine/core/engine.hpp"
#include "engine/core/rng.hpp"
#include "engine/core/validate.hpp"
#include "engine/ffi/convert.hpp"
#include "engine/items/item.hpp"
#include "engine/model/move.hpp"
#include "engine/model/pokemon.hpp"

#include <cstdint>
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

// Stateless and holding a const reference to the read-only loader, so N
// battles can run concurrently on distinct BattleStates without a lock (D7).
const BattleEngine &engine() {
  static const BattleEngine instance{loaderStorage()};
  loader(); // refuse to hand out an engine over an unloaded catalog
  return instance;
}

// Prefix rewriting is the only channel Rust has: cxx transports what() and
// loses the exception type (FFI-CONTRACT.md section 9).
template <typename F> void rethrowAs(const char *prefix, F &&f) {
  try {
    f();
  } catch (const std::exception &e) {
    throw std::invalid_argument(std::string(prefix) + ": " + e.what());
  }
}

void requireId(bool ok, const char *what, int id, int count) {
  if (!ok)
    throw std::out_of_range("E_ARG: " + std::string(what) + " id " + std::to_string(id) +
                            " out of range [0, " + std::to_string(count) + ")");
}

constexpr uint64_t kFnvOffset = 14695981039346656037ULL;
constexpr uint64_t kFnvPrime = 1099511628211ULL;

void fnvFeed(uint64_t &h, const std::string &s) {
  // std::string is signed char on this target: the cast must be explicit, and
  // it must go through unsigned char so bytes above 0x7F feed the same value
  // on every platform. The fingerprint is worthless if it is not portable.
  for (char ch : s) {
    h ^= static_cast<uint64_t>(static_cast<unsigned char>(ch));
    h *= kFnvPrime;
  }
  // NUL separator: without it "AB" + "C" and "A" + "BC" would collide.
  h *= kFnvPrime;
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

SpeciesEntry species_entry(int id) {
  const DataLoader &d = loader();
  requireId(d.isValidSpeciesId(id), "species", id, d.speciesCount());
  const Species &sp = d.speciesByIndex(id);
  SpeciesEntry e;
  e.id_string = sp.id;
  e.display_name = sp.displayName;
  e.type1 = static_cast<int32_t>(sp.type1);
  e.type2 = static_cast<int32_t>(sp.type2);
  e.weight_kg = sp.weightKg;
  e.mega = sp.mega;
  e.legendary = sp.legendary;
  return e;
}

MoveEntry move_entry(int id) {
  const DataLoader &d = loader();
  requireId(d.isValidMoveId(id), "move", id, d.moveCount());
  const Move &m = d.moveByIndex(id);
  MoveEntry e;
  e.name = m.name;
  e.type = static_cast<int32_t>(m.type);
  e.category = static_cast<int32_t>(m.category);
  e.power = m.power;
  e.accuracy = m.accuracy;
  e.pp = m.pp;
  e.priority = m.priority;
  return e;
}

BattlePokemon make_combatant(int species_id) {
  const DataLoader &d = loader();
  requireId(d.isValidSpeciesId(species_id), "species", species_id, d.speciesCount());

  const Species &sp = d.speciesByIndex(species_id);
  BattlePokemon p;
  p.species_id = species_id;
  p.level = kBattleLevel;
  p.stats = computeSpeciesStats(sp, kBattleLevel);
  p.currentHp = p.stats.hp;
  p.item_id = sp.item.empty() ? kNoItem : d.findItemId(sp.item);

  for (size_t i = 0; i < sp.movepool.size() && i < kMaxMovesPerPokemon; ++i) {
    int mid = d.findMoveId(sp.movepool[i]);
    if (mid < 0)
      throw std::runtime_error("E_DATA: species '" + sp.id + "' lists move '" + sp.movepool[i] +
                               "' which is not in the catalog");
    p.move_ids[i] = mid;
    p.pp[i] = d.moveByIndex(mid).pp;
  }
  return p;
}

void validate_team(const std::array<BattlePokemon, kTeamSize> &team, int team_size) {
  const DataLoader &d = loader();
  rethrowAs("E_TEAM", [&] { validateTeam(team, team_size, d); });
}

void validate_state(const BattleState &state) {
  const DataLoader &d = loader();
  rethrowAs("E_STATE", [&] { validateState(state, d); });
}

std::vector<FfiEvent> start_battle(BattleState &state, uint64_t seed) {
  validate_state(state);
  BattleState scratch = state;
  MersenneRNG rng(seed);
  EventLog events = engine().startBattle(scratch, rng);
  std::vector<FfiEvent> flat = flatten(events);
  state = scratch; // commit only once nothing has thrown (D8)
  return flat;
}

std::vector<FfiEvent> resolve_turn(BattleState &state, FfiAction a0, FfiAction a1, uint64_t seed) {
  validate_state(state);
  const Action p0 = toAction(a0);
  const Action p1 = toAction(a1);
  BattleState scratch = state;
  MersenneRNG rng(seed);
  EventLog events;
  rethrowAs("E_ACTION", [&] { events = engine().resolveTurn(scratch, p0, p1, rng); });
  std::vector<FfiEvent> flat = flatten(events);
  state = scratch;
  return flat;
}

std::vector<FfiEvent> resolve_replacement(BattleState &state, int side, int team_index) {
  validate_state(state);
  BattleState scratch = state;
  EventLog events;
  rethrowAs("E_ACTION", [&] { events = engine().resolveReplacement(scratch, side, team_index); });
  std::vector<FfiEvent> flat = flatten(events);
  state = scratch;
  return flat;
}

int faster_side(const BattleState &state, uint64_t seed) {
  validate_state(state);
  MersenneRNG rng(seed);
  return engine().fasterSide(state, rng);
}

bool is_over(const BattleState &state) { return state.isOver(); }

bool side_has_lost(const BattleState &state, int side) {
  if (side != 0 && side != 1)
    throw std::out_of_range("E_ARG: side " + std::to_string(side) + " out of range [0, 2)");
  return state.sideHasLost(side);
}

int struggle_move_id() { return kFfiStruggle; }
int struggle_recoil_ability_id() { return kFfiStruggleRecoil; }

uint64_t catalog_fingerprint() {
  const DataLoader &d = loader();
  uint64_t h = kFnvOffset;

  fnvFeed(h, std::to_string(d.speciesCount()));
  for (int i = 0; i < d.speciesCount(); ++i)
    fnvFeed(h, d.speciesByIndex(i).id);

  fnvFeed(h, std::to_string(d.moveCount()));
  for (int i = 0; i < d.moveCount(); ++i)
    fnvFeed(h, d.moveByIndex(i).name);

  fnvFeed(h, std::to_string(engine::itemCount()));
  for (int i = 0; i < engine::itemCount(); ++i)
    fnvFeed(h, engine::itemByIndex(i)->name());

  fnvFeed(h, std::to_string(engine::abilityCount()));
  for (int i = 0; i < engine::abilityCount(); ++i)
    fnvFeed(h, engine::abilityByIndex(i)->name());

  return h;
}

} // namespace engine::ffi
