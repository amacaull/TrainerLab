#include "search/encoding.hpp"
#include "search/game.hpp"

#include "engine/core/observation.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <set>
#include <string>
#include <vector>

namespace {
const search::Game &game() {
  static const search::Game instance(TRAINERLAB_DATA_DIR);
  return instance;
}
} // namespace

TEST_CASE("every column has its own name") {
  const search::Encoding &e = game().encoding();
  const std::set<std::string> pokemon(e.pokemonNames().begin(), e.pokemonNames().end());
  const std::set<std::string> field(e.fieldNames().begin(), e.fieldNames().end());
  CHECK(pokemon.size() == e.pokemonNames().size());
  CHECK(field.size() == e.fieldNames().size());
}

TEST_CASE("a foe not seen is present and nothing else; a slot past the team is empty") {
  const search::Encoding &e = game().encoding();
  engine::BattleState state = game().randomBattle(2);
  state.team_size[1] = 4;
  const engine::BattleState view = engine::observe(state, 0);
  const size_t width = e.pokemonNames().size();
  std::vector<float> pokemon(search::Encoding::kSlots * width);
  std::vector<int32_t> ids(search::Encoding::kSlots * search::Encoding::kIds);
  std::vector<float> field(e.fieldNames().size());

  e.encode(view, 0, pokemon.data(), ids.data(), field.data());

  auto sum = [&](int slot) {
    float total = 0;
    for (size_t c = 0; c < width; ++c)
      total += pokemon[static_cast<size_t>(slot) * width + c];
    return total;
  };
  CHECK(pokemon[0] == 1.0f);                         // my lead: present
  CHECK(ids[0] == state.teams[0][0].species_id + 1); // and known
  CHECK(sum(7) == 1.0f);                             // the foe's second, unseen: present only
  CHECK(ids[7 * search::Encoding::kIds] == 0);
  CHECK(sum(11) == 0.0f); // past the foe's four
}

TEST_CASE("the mask marks moves by slot and switches by team index") {
  std::vector<float> mask(search::Encoding::kActions);
  search::Encoding::mask({engine::UseMove{2}, engine::SwitchAction{5}}, mask.data());

  const std::vector<float> expected{0, 0, 1, 0, 0, 0, 0, 0, 0, 1};
  CHECK(mask == expected);
}

TEST_CASE("the matchup columns follow the others: what each Pokemon's moves would do to the "
          "opposing active, who is faster, the most the opposing active can take from it") {
  const search::Encoding &e = game().encoding();
  const auto &names = e.pokemonNames();
  const size_t width = names.size();
  REQUIRE(width == 163);
  CHECK(names[152] == "move4_type_fairy"); // the columns of before, unchanged
  const std::vector<std::string> matchups{
      "vs_faster",    "move1_effect", "move1_damage", "move2_effect", "move2_damage",
      "move3_effect", "move3_damage", "move4_effect", "move4_damage", "vs_best_taken"};
  CHECK(std::vector<std::string>(names.end() - 10, names.end()) == matchups);
  auto column = [&](const std::string &name) {
    return static_cast<size_t>(std::find(names.begin(), names.end(), name) - names.begin());
  };

  const engine::DataLoader &data = game().data();
  for (const engine::BattleState &state : search::openings(game(), 12)) {
    const engine::BattleState view = engine::observe(state, 0);
    std::vector<float> pokemon(search::Encoding::kSlots * width);
    std::vector<int32_t> ids(search::Encoding::kSlots * search::Encoding::kIds);
    std::vector<float> field(e.fieldNames().size());
    e.encode(view, 0, pokemon.data(), ids.data(), field.data());

    const size_t mine = static_cast<size_t>(view.activeIndex[0]);
    const size_t theirs = 6 + static_cast<size_t>(view.activeIndex[1]);
    auto at = [&](size_t slot, const std::string &name) {
      return pokemon[slot * width + column(name)];
    };
    // Each side's active against the other's: the effectiveness of each move (over 4), and some
    // damage exactly when the move has power and the target is not immune.
    for (int p = 0; p < 2; ++p) {
      const auto side = static_cast<size_t>(p), other = static_cast<size_t>(1 - p);
      const engine::BattlePokemon &user =
          view.teams[side][static_cast<size_t>(view.activeIndex[side])];
      const engine::BattlePokemon &target =
          view.teams[other][static_cast<size_t>(view.activeIndex[other])];
      const engine::Species &defender = data.speciesByIndex(target.species_id);
      const size_t slot = p == 0 ? mine : theirs;
      float best = 0.0f;
      for (int m = 0; m < 4; ++m) {
        const std::string move = "move" + std::to_string(m + 1);
        const int id = user.move_ids[static_cast<size_t>(m)];
        if (id == engine::kNoMove) {
          CHECK(at(slot, move + "_damage") == 0.0f);
          continue;
        }
        const engine::Move &mv = data.moveByIndex(id);
        const float effect =
            data.typeChart().effectiveness(mv.type, defender.type1, defender.type2);
        CHECK(at(slot, move + "_effect") == effect / 4);
        const bool hurts =
            mv.power > 0 && mv.category != engine::MoveCategory::Status && effect > 0;
        CHECK((at(slot, move + "_damage") > 0.0f) == hurts);
        CHECK(at(slot, move + "_damage") <= 2.0f);
        best = std::max(best, at(slot, move + "_damage"));
      }
      // What the other active takes at most: this active's best, as a share of its HP.
      CHECK(at(p == 0 ? theirs : mine, "vs_best_taken") == best);
    }
    CHECK(at(mine, "vs_faster") + at(theirs, "vs_faster") == 1.0f);
  }
}
