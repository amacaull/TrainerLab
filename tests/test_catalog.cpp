#include "engine/data_loader.hpp"
#include "engine/pokemon.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace engine;

TEST_CASE("DataLoader: catalog sizes match expected content", "[catalog]") {
  DataLoader data;
  data.loadAll(BATTLE_ENGINE_DATA_DIR);

  REQUIRE(data.speciesCount() == 8);
  REQUIRE(data.moveCount() == 22);
}

TEST_CASE("DataLoader: lookup by name returns valid id, miss returns -1", "[catalog]") {
  DataLoader data;
  data.loadAll(BATTLE_ENGINE_DATA_DIR);

  int chId = data.findSpeciesId("charizard");
  REQUIRE(chId >= 0);
  REQUIRE(data.isValidSpeciesId(chId));
  REQUIRE(data.speciesByIndex(chId).displayName == "Charizard");

  int flameId = data.findMoveId("Flamethrower");
  REQUIRE(flameId >= 0);
  REQUIRE(data.isValidMoveId(flameId));
  REQUIRE(data.moveByIndex(flameId).name == "Flamethrower");

  REQUIRE(data.findSpeciesId("nosuchpokemon") == -1);
  REQUIRE(data.findMoveId("NoSuchMove") == -1);
}

// Catalog ids must be stable across loads. The FFI client (Rust) caches
// ids at startup and relies on them not shifting between engine restarts.
TEST_CASE("DataLoader: catalog order is deterministic across loads", "[catalog]") {
  DataLoader data1;
  DataLoader data2;
  data1.loadAll(BATTLE_ENGINE_DATA_DIR);
  data2.loadAll(BATTLE_ENGINE_DATA_DIR);

  REQUIRE(data1.speciesCount() == data2.speciesCount());
  REQUIRE(data1.moveCount() == data2.moveCount());

  REQUIRE(data1.findSpeciesId("charizard") == data2.findSpeciesId("charizard"));
  REQUIRE(data1.findSpeciesId("venusaur") == data2.findSpeciesId("venusaur"));
  REQUIRE(data1.findMoveId("Flamethrower") == data2.findMoveId("Flamethrower"));
  REQUIRE(data1.findMoveId("Earthquake") == data2.findMoveId("Earthquake"));
}

TEST_CASE("DataLoader: speciesByIndex / moveByIndex throw on invalid id", "[catalog]") {
  DataLoader data;
  data.loadAll(BATTLE_ENGINE_DATA_DIR);

  REQUIRE_THROWS_AS(data.speciesByIndex(-1), std::out_of_range);
  REQUIRE_THROWS_AS(data.speciesByIndex(99999), std::out_of_range);
  REQUIRE_THROWS_AS(data.moveByIndex(-1), std::out_of_range);
  REQUIRE_THROWS_AS(data.moveByIndex(99999), std::out_of_range);
}

TEST_CASE("DataLoader: every species has a valid movepool", "[catalog][data]") {
  DataLoader data;
  data.loadAll(BATTLE_ENGINE_DATA_DIR);

  for (int sid = 0; sid < data.speciesCount(); ++sid) {
    const Species &sp = data.speciesByIndex(sid);
    INFO("species " << sp.id);
    REQUIRE_FALSE(sp.movepool.empty());
    for (const auto &moveName : sp.movepool) {
      INFO("  move " << moveName);
      REQUIRE(data.findMoveId(moveName) >= 0);
    }
  }
}
