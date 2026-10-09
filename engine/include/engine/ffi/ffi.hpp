#pragma once

#include "engine/core/battle_state.hpp"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

// Every function may throw. Messages start with a stable prefix (E_INIT, E_DATA, E_ARG, E_STATE,
// E_TEAM, E_ACTION): callers across a language boundary only get what(). Contract: README.md,
// section 6.
namespace engine::ffi {
// Idempotent for the same path. Another path throws: two catalogs in one process would mean two id
// spaces.
void engine_init(const std::string &dataDir);

bool engine_is_initialised();
std::string engine_data_dir();

int species_count();
int move_count();
int item_count();
int ability_count();

// A miss is -1, not an error. Species take the id_string (the JSON filename), not the display name.
int find_species_id(const std::string &id_string);
int find_move_id(const std::string &name);
int find_item_id(const std::string &name);
int find_ability_id(const std::string &name);

// E_ARG on an invalid id: unlike a lookup miss, that is a caller bug.
std::string species_id_string(int id);
std::string species_display_name(int id);
std::string move_name(int id);
std::string item_name(int id);
std::string ability_name(int id);

// Reserved name_id values. Any other negative name_id is a bug.
constexpr int kFfiNoName = -1;
constexpr int kFfiStruggle = -2;
constexpr int kFfiStruggleRecoil = -3;

int struggle_move_id();
int struggle_recoil_ability_id();

// Anything that persists catalog ids stores it and rechecks it on load. It covers the names and
// their order only.
uint64_t catalog_fingerprint();

struct FfiAction {
  uint8_t kind = 0;          // 0 = UseMove, 1 = Switch
  int32_t index = 0;         // kind 0: move slot 0-3 | kind 1: team index 0-5
  int32_t pivot_target = -1; // kind 0 only, -1 = auto
};

// The meaning of i0/i1/f0/flags depends on kind: see the event table in README.md, section 6.
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

// Enums travel as int; their numbering is frozen.
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

SpeciesEntry species_entry(int id);
MoveEntry move_entry(int id);

BattlePokemon make_combatant(int species_id);

void validate_team(const std::array<BattlePokemon, kTeamSize> &team, int team_size);
void validate_state(const BattleState &state);

// Each validates the state, works on a copy and commits only on success: a throw never leaves the
// caller's BattleState half-written.
std::vector<FfiEvent> start_battle(BattleState &state, uint64_t seed);
std::vector<FfiEvent> resolve_turn(BattleState &state, FfiAction a0, FfiAction a1, uint64_t seed);
std::vector<FfiEvent> resolve_replacement(BattleState &state, int side, int team_index);

// The engine imposes no order on simultaneous replacements: the caller asks here and decides.
int faster_side(const BattleState &state, uint64_t seed);

bool is_over(const BattleState &state);
bool side_has_lost(const BattleState &state, int side);

// What resolve_turn accepts for this side, one entry per distinct outcome (README.md, section 6).
// Empty when the active is fainted: ask legal_replacements instead.
std::vector<FfiAction> legal_actions(const BattleState &state, int side);

// The team indices resolve_replacement accepts. Empty unless the active is fainted.
std::vector<int> legal_replacements(const BattleState &state, int side);

// The state as the player on `side` sees it (engine/core/observation.hpp). A view for decisions:
// never pass it back to any function here, legal_actions included (validate_state rejects it).
// Whoever owns the real state computes the legal actions and hands both to the player.
BattleState observe(const BattleState &state, int side);
} // namespace engine::ffi
