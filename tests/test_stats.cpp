#include "engine/stats.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace engine;

TEST_CASE("Stats at level 50", "[stats]") {
    // HP formula: (2*78*50)/100 + 50 + 10 = 138
    Stats base{78, 84, 78, 109, 85, 100};
    Stats s = computeStats(base, 50);
    REQUIRE(s.hp == 138);

    // Other stats: (2*base*50)/100 + 5 = base + 5
    REQUIRE(s.atk     == 89);
    REQUIRE(s.def     == 83);
    REQUIRE(s.specAtk == 114);
    REQUIRE(s.specDef == 90);
    REQUIRE(s.speed   == 105);
}

TEST_CASE("Stats at level 100", "[stats]") {
    Stats base{78, 84, 78, 109, 85, 100};
    Stats s = computeStats(base, 100);
    REQUIRE(s.hp    == 266);
    REQUIRE(s.atk   == 173);
    REQUIRE(s.speed == 205);
}
