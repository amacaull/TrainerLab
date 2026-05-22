#include "engine/types.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace engine;

TEST_CASE("Type <-> string conversion", "[types]") {
    REQUIRE(typeName(Type::Fire)  == "Fire");
    REQUIRE(typeName(Type::Grass) == "Grass");
    REQUIRE(typeFromString("Fire")  == Type::Fire);
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
    chart.set(Type::Fire, Type::Grass,  2.0f);
    chart.set(Type::Fire, Type::Poison, 1.0f);
    chart.set(Type::Fire, Type::Water,  0.5f);

    // Grass/Poison vs Fire = 2.0 * 1.0 = 2.0
    REQUIRE(chart.effectiveness(Type::Fire, Type::Grass, Type::Poison) == 2.0f);

    // Mono-type Grass vs Fire = 2.0 (not 4.0)
    REQUIRE(chart.effectiveness(Type::Fire, Type::Grass, Type::Grass) == 2.0f);
}
