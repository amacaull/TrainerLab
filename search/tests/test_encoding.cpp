#include "search/encoding.hpp"
#include "search/game.hpp"

#include "engine/core/observation.hpp"

#include <catch2/catch_test_macros.hpp>

#include <set>
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
