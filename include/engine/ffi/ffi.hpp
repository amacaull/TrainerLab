#pragma once

#include "engine/core/battle_state.hpp"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

// Boundary layer for the Rust backend (cxx, ADR D1). Plain C++ on purpose:
// the bridge and its rust::Str adapter live on the caller's side, so any
// other binding could consume this header unchanged.
//
// Every function may throw. Messages carry a stable prefix - E_INIT, E_DATA,
// E_ARG, E_STATE, E_TEAM, E_ACTION - because cxx transports what() only.
// Full contract: README.md, section 6.
namespace engine::ffi {

// Idempotent for the same path. A different path throws: two catalogs in one
// process would mean two id spaces (ADR #57).
void engine_init(const std::string &dataDir);

bool engine_is_initialised();
std::string engine_data_dir();

int species_count();
int move_count();
int item_count();
int ability_count();

// A miss is -1, not an error; only an uninitialised engine throws. Species
// take the id_string - the JSON filename, English canon (ADR #48) - not the
// display name.
int find_species_id(const std::string &id_string);
int find_move_id(const std::string &name);
int find_item_id(const std::string &name);
int find_ability_id(const std::string &name);

// Throw E_ARG on an invalid id: unlike a lookup miss, that is a caller bug.
std::string species_id_string(int id);
std::string species_display_name(int id);
std::string move_name(int id);
std::string item_name(int id);
std::string ability_name(int id);

// Reserved name_id values. Struggle is hardcoded (ADR #35), so it has no
// catalog id, and -1 already means "this event carries no name" - hence a
// distinct sentinel rather than an overload. Any other negative name_id is a
// bug. See README.md, section 6 (sentinels).
constexpr int kFfiNoName = -1;
constexpr int kFfiStruggle = -2;
constexpr int kFfiStruggleRecoil = -3;

int struggle_move_id();
int struggle_recoil_ability_id();

// FNV-1a 64 over the four catalogs, in the order species, moves, items,
// abilities (ADR #58). Rust stores it when a battle is created and rechecks
// it on load: species and move ids are the alphabetical order of the files in
// data/, so adding one Pokemon silently reinterprets every saved BattleState.
//
// Hand-rolled on purpose: std::hash is not stable across platforms or
// libstdc++ versions, which is exactly what this must survive.
uint64_t catalog_fingerprint();

// One action, flat. The variant tag becomes a value: a variant has no
// guaranteed layout and cannot cross.
struct FfiAction {
  uint8_t kind = 0;          // 0 = UseMove, 1 = Switch
  int32_t index = 0;         // kind 0: move slot 0-3 | kind 1: team index 0-5
  int32_t pivot_target = -1; // kind 0 only, -1 = auto
};

// One event, flat. The meaning of i0/i1/f0/flags depends on kind - the table
// in README.md section 6 IS the contract, and the kind numbering there
// is authoritative (it does not derive from the variant's order).
struct FfiEvent {
  uint8_t kind = 0;
  int8_t side = -1; // -1 for field-wide events
  int8_t slot = -1;
  int32_t name_id = kFfiNoName; // move_id | item_id | ability_id, per kind
  int32_t i0 = 0;
  int32_t i1 = 0;
  float f0 = 0.0f;    // type effectiveness only
  uint8_t flags = 0;  // bit 0 = STAB, bit 1 = crit
};

constexpr uint8_t kFfiFlagStab = 1u << 0;
constexpr uint8_t kFfiFlagCrit = 1u << 1;

// Catalog entries for the frontend and the AI service. Enums travel as int:
// their numbering is frozen (README.md section 6, "Ce qui est gelé").
struct SpeciesEntry {
  std::string id_string;
  std::string display_name;
  int32_t type1 = 0;
  int32_t type2 = 0;
  double weight_kg = 0.0;
  bool mega = false;
  bool legendary = false;
};

struct MoveEntry {
  std::string name;
  int32_t type = 0;
  int32_t category = 0;
  int32_t power = 0;
  int32_t accuracy = 0;
  int32_t pp = 0;
  int32_t priority = 0;
};

SpeciesEntry species_entry(int id); // E_ARG on an invalid id
MoveEntry move_entry(int id);

// A complete combatant: stats, held item, the species' four moves and their
// PP (ADR #33/#34). No level parameter and no separate move setter - the
// movepool is exactly four and everyone fights at 100, so there is nothing to
// choose. Callers must never assemble a BattlePokemon field by field: that is
// how a second, divergent implementation of the stat formula gets born.
BattlePokemon make_combatant(int species_id);

void validate_team(const std::array<BattlePokemon, kTeamSize> &team, int team_size);
void validate_state(const BattleState &state);

// Each of these validates the state first (ADR #13), then works on a copy and
// commits only on success (D8): a throw mid-turn must not leave the caller's
// BattleState half-written.
std::vector<FfiEvent> start_battle(BattleState &state, uint64_t seed);
std::vector<FfiEvent> resolve_turn(BattleState &state, FfiAction a0, FfiAction a1, uint64_t seed);
std::vector<FfiEvent> resolve_replacement(BattleState &state, int side, int team_index);

// Ordering for simultaneous replacements (D6): the engine does not impose one,
// the caller asks and decides.
int faster_side(const BattleState &state, uint64_t seed);

bool is_over(const BattleState &state);
bool side_has_lost(const BattleState &state, int side);

} // namespace engine::ffi
