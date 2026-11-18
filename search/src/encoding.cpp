#include "search/encoding.hpp"

#include "engine/model/move.hpp"

#include <algorithm>
#include <array>
#include <variant>

namespace search {
namespace {
using engine::BattlePokemon;

constexpr std::array kTypes{"normal",   "fire",   "water",  "electric", "grass",   "ice",
                            "fighting", "poison", "ground", "flying",   "psychic", "bug",
                            "rock",     "ghost",  "dragon", "dark",     "steel",   "fairy"};
constexpr std::array kStats{"hp", "atk", "def", "spa", "spd", "spe"};
constexpr std::array kStages{"atk", "def", "spa", "spd", "spe", "accuracy", "evasion"};
constexpr std::array kStatuses{"burn", "poison", "toxic", "paralysis", "sleep", "freeze"};
constexpr std::array kWeathers{"rain", "sun", "sand", "snow", "strong_winds"};

constexpr double kStatScale = 500.0;  // a stat of 500 reads 1 (Blissey's HP, 714, reads 1.43)
constexpr double kPowerScale = 150.0; // power 150 reads 1
constexpr double kPriorityScale = 5.0;

size_t at(int i) { return static_cast<size_t>(i); }
int32_t idOf(int catalogIndex) { return catalogIndex < 0 ? 0 : catalogIndex + 1; }

// Writes values one after the other: the order is the order of the names.
struct Writer {
  float *out;
  void put(double value) { *out++ = static_cast<float>(value); }
  void oneHot(int index, size_t size) {
    for (size_t i = 0; i < size; ++i)
      put(static_cast<int>(i) == index ? 1.0 : 0.0);
  }
};
} // namespace

Encoding::Encoding(const engine::DataLoader &data) : data_(data) {
  auto &p = pokemonNames_;
  for (const char *n : {"present", "known", "active", "fainted", "hp"})
    p.push_back(n);
  for (const char *s : kStats)
    p.push_back(std::string("stat_") + s);
  for (const char *s : kStages)
    p.push_back(std::string("stage_") + s);
  for (const char *s : kStatuses)
    p.push_back(std::string("status_") + s);
  for (const char *n : {"has_item", "choice_locked", "charging"})
    p.push_back(n);
  for (const char *t : kTypes)
    p.push_back(std::string("type_") + t);
  for (int m = 1; m <= engine::kMaxMovesPerPokemon; ++m) {
    const std::string move = "move" + std::to_string(m) + "_";
    for (const char *n : {"present", "power", "accuracy", "never_misses", "physical", "special",
                          "status", "priority", "pp"})
      p.push_back(move + n);
    for (const char *t : kTypes)
      p.push_back(move + "type_" + t);
  }

  auto &f = fieldNames_;
  for (const char *w : kWeathers)
    f.push_back(std::string("weather_") + w);
  f.push_back("terrain_electric");
  f.push_back("turn");
  for (const char *side : {"my_", "foe_"})
    for (const char *n : {"stealth_rock", "spikes", "toxic_spikes", "aurora_veil", "wish"})
      f.push_back(std::string(side) + n);
}

void Encoding::encodePokemon(const BattlePokemon &p, bool present, bool active, float *row,
                             int32_t *ids) const {
  std::fill(row, row + pokemonNames_.size(), 0.0f);
  std::fill(ids, ids + kIds, 0);
  if (!present)
    return;
  Writer w{row};
  w.put(1.0); // present
  if (p.isEmpty())
    return; // a foe not seen yet: present, nothing else known
  const engine::Species &species = data_.speciesByIndex(p.species_id);
  w.put(1.0);
  w.put(active);
  w.put(p.isFainted());
  w.put(static_cast<double>(p.currentHp) / p.stats.hp);
  for (int stat :
       {p.stats.hp, p.stats.atk, p.stats.def, p.stats.specAtk, p.stats.specDef, p.stats.speed})
    w.put(stat / kStatScale);
  for (int stage : p.stat_stages)
    w.put(stage / 6.0);
  w.oneHot(static_cast<int>(p.status) - 1, kStatuses.size());
  const bool hasItem = p.item_id != engine::kNoItem && !p.item_consumed;
  w.put(hasItem);
  w.put(p.locked_move_id != engine::kNoMove);
  w.put(p.charging_move_id != engine::kNoMove);
  for (size_t t = 0; t < kTypes.size(); ++t)
    w.put(static_cast<int>(species.type1) == static_cast<int>(t) ||
          static_cast<int>(species.type2) == static_cast<int>(t));

  ids[0] = idOf(p.species_id);
  ids[1] = idOf(species.abilityId);
  ids[2] = idOf(hasItem ? p.item_id : -1);
  for (int m = 0; m < engine::kMaxMovesPerPokemon; ++m) {
    const int moveId = p.move_ids[at(m)];
    ids[3 + m] = idOf(moveId);
    if (moveId == engine::kNoMove) {
      w.out += 9 + kTypes.size();
      continue;
    }
    const engine::Move &move = data_.moveByIndex(moveId);
    w.put(1.0);
    w.put(move.power / kPowerScale);
    w.put(move.accuracy > 0 ? move.accuracy / 100.0 : 1.0);
    w.put(move.accuracy <= 0);
    w.oneHot(static_cast<int>(move.category), 3);
    w.put(move.priority / kPriorityScale);
    w.put(move.pp > 0 ? static_cast<double>(p.pp[at(m)]) / move.pp : 0.0);
    w.oneHot(static_cast<int>(move.type), kTypes.size());
  }
}

void Encoding::encode(const engine::BattleState &view, int side, float *pokemon, int32_t *ids,
                      float *field) const {
  const size_t width = pokemonNames_.size();
  for (int p = 0; p < 2; ++p) {
    const int s = p == 0 ? side : 1 - side;
    for (int i = 0; i < engine::kTeamSize; ++i) {
      const int slot = p * engine::kTeamSize + i;
      encodePokemon(view.teams[at(s)][at(i)], i < view.team_size[at(s)],
                    i == view.activeIndex[at(s)], pokemon + at(slot) * width,
                    ids + at(slot) * kIds);
    }
  }

  Writer w{field};
  w.oneHot(static_cast<int>(view.weather) - 1, kWeathers.size());
  w.put(view.terrain != engine::Terrain::None);
  w.put(view.turn / 100.0);
  for (int s : {side, 1 - side}) {
    const engine::SideHazards &hazards = view.hazards[at(s)];
    w.put(hazards.stealth_rock);
    w.put(hazards.spikes / 3.0);
    w.put(hazards.toxic_spikes / 2.0);
    w.put(view.aurora_veil_turns[at(s)] / 8.0);
    w.put(view.wish_turns[at(s)] > 0);
  }
}

void Encoding::mask(const std::vector<engine::Action> &legal, float *out) {
  std::fill(out, out + kActions, 0.0f);
  for (const engine::Action &action : legal) {
    if (const auto *move = std::get_if<engine::UseMove>(&action))
      out[at(move->moveIndex)] = 1.0f;
    else
      out[at(engine::kMaxMovesPerPokemon + std::get<engine::SwitchAction>(action).teamIndex)] =
          1.0f;
  }
}
} // namespace search
