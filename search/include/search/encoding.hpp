#pragma once

#include "engine/core/battle_state.hpp"
#include "engine/core/data_loader.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace search {
// A position as a network reads it: one vector per Pokemon, so that the same weights read every
// Pokemon and can learn what each one is for, plus the field and the legal options.
//
// From `side`'s view (its own observe), in slots: its team first, then the foe's, each in team
// order (slot 0 to 5 of each team), with a flag for the active. A foe Pokemon not seen yet is
// present but unknown: every other value 0. Slots past a team's size are absent: all 0.
//
// Next to the numbers, each Pokemon has seven catalog ids for embeddings: species, ability, item,
// then its four moves. 0 means none or unknown, any other value is the catalog index + 1.
class Encoding {
public:
  static constexpr int kSlots = 2 * engine::kTeamSize;
  static constexpr int kIds = 7;
  static constexpr int kActions =
      engine::kMaxMovesPerPokemon + engine::kTeamSize; // 4 moves, 6 switches

  explicit Encoding(const engine::DataLoader &data);

  // The name of each column of a Pokemon's vector, and of the field's.
  const std::vector<std::string> &pokemonNames() const { return pokemonNames_; }
  const std::vector<std::string> &fieldNames() const { return fieldNames_; }

  // Writes kSlots x pokemonNames().size() floats, kSlots x kIds ids and fieldNames().size()
  // floats.
  void encode(const engine::BattleState &view, int side, float *pokemon, int32_t *ids,
              float *field) const;

  // 1 for each legal option, 0 elsewhere: moves 0-3 by slot, then switches by team index (4-9).
  static void mask(const std::vector<engine::Action> &legal, float *out);

private:
  void encodePokemon(const engine::BattlePokemon &p, bool present, bool active, float *row,
                     int32_t *ids) const;

  const engine::DataLoader &data_;
  std::vector<std::string> pokemonNames_;
  std::vector<std::string> fieldNames_;
};
} // namespace search
