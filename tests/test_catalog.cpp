#include "helpers.hpp"

#include "engine/core/data_loader.hpp"
#include "engine/model/pokemon.hpp"

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

using namespace engine;

// The shipped catalog is exactly the team sheet: nothing more ships to the
// frontend, and nothing extra takes an index in the table frozen for the
// FFI (ADR #49). The test instruments live in tests/fixtures.
TEST_CASE("The shipped catalog is exactly the roster", "[catalog][data]") {
  DataLoader data;
  data.loadAll(BATTLE_ENGINE_DATA_DIR);

  REQUIRE(data.speciesCount() == 49);
  REQUIRE(data.moveCount() == 95);

  // Every shipped move is reachable: it belongs to at least one movepool.
  std::vector<bool> reachable(static_cast<size_t>(data.moveCount()), false);
  for (int sid = 0; sid < data.speciesCount(); ++sid)
    for (const auto &moveName : data.speciesByIndex(sid).movepool)
      reachable[static_cast<size_t>(data.findMoveId(moveName))] = true;
  for (int mid = 0; mid < data.moveCount(); ++mid) {
    INFO("move " << data.moveByIndex(mid).name);
    REQUIRE(reachable[static_cast<size_t>(mid)]);
  }
}

TEST_CASE("Test fixtures add instruments on top of the shipped catalog", "[catalog]") {
  DataLoader data;
  engine::test::loadAll(data);

  REQUIRE(data.speciesCount() == 49);   // the roster species ARE the fixtures
  REQUIRE(data.moveCount() == 95 + 18); // plus the neutral test instruments
  REQUIRE(data.findMoveId("Tackle") >= 0);
}

TEST_CASE("DataLoader: lookup by name returns valid id, miss returns -1", "[catalog]") {
  DataLoader data;
  engine::test::loadAll(data);

  int chId = data.findSpeciesId("Infernape");
  REQUIRE(chId >= 0);
  REQUIRE(data.isValidSpeciesId(chId));
  REQUIRE(data.speciesByIndex(chId).displayName == "Infernape");

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
  engine::test::loadAll(data1);
  engine::test::loadAll(data2);

  REQUIRE(data1.speciesCount() == data2.speciesCount());
  REQUIRE(data1.moveCount() == data2.moveCount());

  REQUIRE(data1.findSpeciesId("Infernape") == data2.findSpeciesId("Infernape"));
  REQUIRE(data1.findSpeciesId("Toxapex") == data2.findSpeciesId("Toxapex"));
  REQUIRE(data1.findMoveId("Flamethrower") == data2.findMoveId("Flamethrower"));
  REQUIRE(data1.findMoveId("Earthquake") == data2.findMoveId("Earthquake"));
}

TEST_CASE("DataLoader: speciesByIndex / moveByIndex throw on invalid id", "[catalog]") {
  DataLoader data;
  engine::test::loadAll(data);

  REQUIRE_THROWS_AS(data.speciesByIndex(-1), std::out_of_range);
  REQUIRE_THROWS_AS(data.speciesByIndex(99999), std::out_of_range);
  REQUIRE_THROWS_AS(data.moveByIndex(-1), std::out_of_range);
  REQUIRE_THROWS_AS(data.moveByIndex(99999), std::out_of_range);
}

TEST_CASE("DataLoader: every species has a valid movepool", "[catalog][data]") {
  DataLoader data;
  engine::test::loadAll(data);

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

TEST_CASE("DataLoader refuses unknown keys and values instead of ignoring them",
          "[catalog][data]") {
  using nlohmann::json;
  // The typo that once sent secondary drops onto the attacker.
  REQUIRE_THROWS(makeEffectFromJson(
      json{{"kind", "StatChange"}, {"stat", "Atk"}, {"delta", -1}, {"affectUser", true}}));
  REQUIRE_THROWS(makeEffectFromJson(
      json{{"kind", "StatChange"}, {"stat", "Atk"}, {"delta", -1}, {"target", "self"}}));
  REQUIRE_THROWS(makeEffectFromJson(json{{"kind", "ClearHazards"}, {"scope", "all"}}));
  REQUIRE_THROWS(makeEffectFromJson(json{{"kind", "Damage"}, {"power", 90}}));
  REQUIRE_NOTHROW(makeEffectFromJson(
      json{{"kind", "StatChange"}, {"stat", "Atk"}, {"delta", -1}, {"chance", 10}}));
}
