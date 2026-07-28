#include "helpers.hpp"

#include "engine/core/data_loader.hpp"
#include "engine/model/types.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace engine;

TEST_CASE("Type <-> string conversion", "[types]") {
  REQUIRE(typeName(Type::Fire) == "Fire");
  REQUIRE(typeName(Type::Grass) == "Grass");
  REQUIRE(typeFromString("Fire") == Type::Fire);
  REQUIRE(typeFromString("Water") == Type::Water);
}

TEST_CASE("TypeChart: basic multipliers", "[types]") {
  TypeChart chart;
  chart.set(Type::Fire, Type::Grass, 2.0f);
  chart.set(Type::Water, Type::Fire, 2.0f);
  chart.set(Type::Fire, Type::Water, 0.5f);

  REQUIRE(chart.multiplier(Type::Fire, Type::Grass) == 2.0f);
  REQUIRE(chart.multiplier(Type::Fire, Type::Water) == 0.5f);
}

TEST_CASE("TypeChart: dual-type effectiveness", "[types]") {
  TypeChart chart;
  chart.set(Type::Fire, Type::Grass, 2.0f);
  chart.set(Type::Fire, Type::Poison, 1.0f);
  chart.set(Type::Fire, Type::Water, 0.5f);

  REQUIRE(chart.effectiveness(Type::Fire, Type::Grass, Type::Poison) == 2.0f);
  REQUIRE(chart.effectiveness(Type::Fire, Type::Grass, Type::Grass) == 2.0f);
}

TEST_CASE("Full type chart loads without error", "[types][data]") {
  DataLoader data;
  REQUIRE_NOTHROW(data.loadAll(BATTLE_ENGINE_DATA_DIR));
}

TEST_CASE("Full type chart: every matchup is a canonical value", "[types][data]") {
  DataLoader data;
  engine::test::loadAll(data);
  const TypeChart &chart = data.typeChart();

  for (int a = 0; a < TypeCount; ++a) {
    for (int d = 0; d < TypeCount; ++d) {
      float m = chart.multiplier(static_cast<Type>(a), static_cast<Type>(d));
      bool isCanonical = (m == 0.0f) || (m == 0.5f) || (m == 1.0f) || (m == 2.0f);
      INFO("attacker=" << typeName(static_cast<Type>(a))
                       << " defender=" << typeName(static_cast<Type>(d)) << " multiplier=" << m);
      REQUIRE(isCanonical);
    }
  }
}

TEST_CASE("Full type chart: canonical immunities (0x)", "[types][data]") {
  DataLoader data;
  engine::test::loadAll(data);
  const TypeChart &chart = data.typeChart();

  REQUIRE(chart.multiplier(Type::Normal, Type::Ghost) == 0.0f);
  REQUIRE(chart.multiplier(Type::Fighting, Type::Ghost) == 0.0f);
  REQUIRE(chart.multiplier(Type::Electric, Type::Ground) == 0.0f);
  REQUIRE(chart.multiplier(Type::Poison, Type::Steel) == 0.0f);
  REQUIRE(chart.multiplier(Type::Ground, Type::Flying) == 0.0f);
  REQUIRE(chart.multiplier(Type::Psychic, Type::Dark) == 0.0f);
  REQUIRE(chart.multiplier(Type::Ghost, Type::Normal) == 0.0f);
  REQUIRE(chart.multiplier(Type::Dragon, Type::Fairy) == 0.0f);
}

TEST_CASE("Full type chart: classic super-effective matchups (2x)", "[types][data]") {
  DataLoader data;
  engine::test::loadAll(data);
  const TypeChart &chart = data.typeChart();

  REQUIRE(chart.multiplier(Type::Fire, Type::Grass) == 2.0f);
  REQUIRE(chart.multiplier(Type::Water, Type::Fire) == 2.0f);
  REQUIRE(chart.multiplier(Type::Grass, Type::Water) == 2.0f);
  REQUIRE(chart.multiplier(Type::Electric, Type::Water) == 2.0f);
  REQUIRE(chart.multiplier(Type::Ice, Type::Dragon) == 2.0f);
  REQUIRE(chart.multiplier(Type::Fighting, Type::Normal) == 2.0f);
  REQUIRE(chart.multiplier(Type::Poison, Type::Fairy) == 2.0f);
  REQUIRE(chart.multiplier(Type::Ground, Type::Electric) == 2.0f);
  REQUIRE(chart.multiplier(Type::Flying, Type::Fighting) == 2.0f);
  REQUIRE(chart.multiplier(Type::Psychic, Type::Fighting) == 2.0f);
  REQUIRE(chart.multiplier(Type::Bug, Type::Psychic) == 2.0f);
  REQUIRE(chart.multiplier(Type::Rock, Type::Flying) == 2.0f);
  REQUIRE(chart.multiplier(Type::Ghost, Type::Psychic) == 2.0f);
  REQUIRE(chart.multiplier(Type::Dragon, Type::Dragon) == 2.0f);
  REQUIRE(chart.multiplier(Type::Dark, Type::Psychic) == 2.0f);
  REQUIRE(chart.multiplier(Type::Steel, Type::Fairy) == 2.0f);
  REQUIRE(chart.multiplier(Type::Fairy, Type::Dragon) == 2.0f);

  REQUIRE(chart.multiplier(Type::Normal, Type::Fighting) == 1.0f);
}

TEST_CASE("Full type chart: classic resistances (0.5x)", "[types][data]") {
  DataLoader data;
  engine::test::loadAll(data);
  const TypeChart &chart = data.typeChart();

  REQUIRE(chart.multiplier(Type::Fire, Type::Water) == 0.5f);
  REQUIRE(chart.multiplier(Type::Water, Type::Grass) == 0.5f);
  REQUIRE(chart.multiplier(Type::Grass, Type::Fire) == 0.5f);
  REQUIRE(chart.multiplier(Type::Steel, Type::Steel) == 0.5f);
  REQUIRE(chart.multiplier(Type::Fairy, Type::Fire) == 0.5f);
  REQUIRE(chart.multiplier(Type::Dark, Type::Fairy) == 0.5f);
}

TEST_CASE("Full type chart: 4x via dual type", "[types][data]") {
  DataLoader data;
  engine::test::loadAll(data);
  const TypeChart &chart = data.typeChart();

  REQUIRE(chart.effectiveness(Type::Ice, Type::Dragon, Type::Flying) == 4.0f);
  REQUIRE(chart.effectiveness(Type::Rock, Type::Fire, Type::Flying) == 4.0f);
}

TEST_CASE("Full type chart: 0.25x via dual resistance", "[types][data]") {
  DataLoader data;
  engine::test::loadAll(data);
  const TypeChart &chart = data.typeChart();

  REQUIRE(chart.effectiveness(Type::Grass, Type::Fire, Type::Flying) == 0.25f);
  REQUIRE(chart.effectiveness(Type::Fire, Type::Water, Type::Dragon) == 0.25f);
}

TEST_CASE("Full type chart: dual-type immunity cancels super-effective", "[types][data]") {
  DataLoader data;
  engine::test::loadAll(data);
  const TypeChart &chart = data.typeChart();

  REQUIRE(chart.effectiveness(Type::Ground, Type::Flying, Type::Steel) == 0.0f);
}
