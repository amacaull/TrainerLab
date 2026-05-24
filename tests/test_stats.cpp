#include "engine/stats.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace engine;

TEST_CASE("Stats at level 50", "[stats]") {
  Stats base{78, 84, 78, 109, 85, 100};
  Stats s = computeStats(base, 50);
  REQUIRE(s.hp == 138);
  REQUIRE(s.atk == 89);
  REQUIRE(s.def == 83);
  REQUIRE(s.specAtk == 114);
  REQUIRE(s.specDef == 90);
  REQUIRE(s.speed == 105);
}

TEST_CASE("Stats at level 100", "[stats]") {
  Stats base{78, 84, 78, 109, 85, 100};
  Stats s = computeStats(base, 100);
  REQUIRE(s.hp == 266);
  REQUIRE(s.atk == 173);
  REQUIRE(s.speed == 205);
}
