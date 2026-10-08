#pragma once

#include "engine/model/stats.hpp"
#include "engine/model/status.hpp"
#include "engine/model/types.hpp"

#include <array>
#include <string>
#include <vector>

namespace engine {
constexpr int kMaxMovesPerPokemon = 4;
constexpr int kNoMove = -1;
constexpr int kNoSpecies = -1;
constexpr int kNoItem = -1;

// Crosses the FFI as int: do not reorder.
enum class StatIndex : int { Atk = 0, Def, SpA, SpD, Spe, Accuracy, Evasion, Count };

constexpr int kStatStageCount = static_cast<int>(StatIndex::Count);

// Everyone fights at 100. Not a parameter on purpose: a hand-built BattlePokemon with a stale level
// would compute wrong damage silently.
constexpr int kBattleLevel = 100;
constexpr int kMaxStage = 6;
constexpr int kMinStage = -6;

struct Species {
  std::string id;
  std::string displayName;
  Stats baseStats;
  Type type1 = Type::Normal;
  Type type2 = Type::Normal;
  std::string ability;
  std::vector<std::string> movepool;

  std::string nature = "Serious";
  std::string item;
  Stats evs;
  double weightKg = 0.0;
  bool legendary = false;
  bool mega = false;

  bool isDualType() const { return type1 != type2; }
};

// FFI contract: POD only (no pointer, vector or string); game content is referenced by catalog
// index.
struct BattlePokemon {
  int species_id = kNoSpecies;
  int level = kBattleLevel;
  Stats stats;
  int currentHp = 0;
  std::array<int, kMaxMovesPerPokemon> move_ids{kNoMove, kNoMove, kNoMove, kNoMove};
  std::array<int, kMaxMovesPerPokemon> pp{};
  int item_id = kNoItem;
  int item_consumed = 0; // persists across switches
  int locked_move_id = kNoMove;
  int disguise_broken = 0; // persists across switches
  int flash_fire_active = 0;
  Status status = Status::None;
  int status_turns = 0;
  int sleep_self_inflicted = 0; // Rest: exempt from the Sleep Clause
  std::array<int, kStatStageCount> stat_stages{};

  int flinched = 0;
  int roosted = 0;
  int protected_now = 0;
  int protect_chain = 0;
  int charging_move_id = kNoMove;
  int invulnerable_state =
      0; // 0 none, 1 Fly, 2 Dig, 3 PhantomForce
  int turns_on_field = 0;
  int last_move_id = kNoMove;
  int destiny_bond_active = 0;
  int protect_contact_status = 0;

  bool isFainted() const { return currentHp <= 0; }
  bool isEmpty() const { return species_id == kNoSpecies; }

  bool hasUsablePp() const {
    for (int i = 0; i < kMaxMovesPerPokemon; ++i)
      if (move_ids[static_cast<size_t>(i)] != kNoMove && pp[static_cast<size_t>(i)] > 0)
        return true;
    return false;
  }
};

Stats computeSpeciesStats(const Species &sp, int level);
} // namespace engine
