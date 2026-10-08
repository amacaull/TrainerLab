#include "engine/model/stats.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace engine;

// Charizard base stats; reference values cross-checked with Showdown's
// damage calc (IV 31, level 100).
static const Stats kBase{78, 84, 78, 109, 85, 100};

TEST_CASE("Stats at level 100, neutral nature, no EVs", "[stats]") {
  Stats s = computeStats(kBase, 100, *natureByName("Serious"), Stats{});
  REQUIRE(s.hp == 297);
  REQUIRE(s.atk == 204);
  REQUIRE(s.def == 192);
  REQUIRE(s.specAtk == 254);
  REQUIRE(s.specDef == 206);
  REQUIRE(s.speed == 236);
}

TEST_CASE("Stats at level 50 still follow the canon formula", "[stats]") {
  Stats s = computeStats(kBase, 50, *natureByName("Serious"), Stats{});
  REQUIRE(s.hp == 153);
  REQUIRE(s.atk == 104);
  REQUIRE(s.speed == 120);
}

TEST_CASE("EVs feed the formula (floor(ev/4))", "[stats]") {
  Stats evs{};
  evs.speed = 252;
  evs.specAtk = 252;
  evs.hp = 4;
  Stats s = computeStats(kBase, 100, *natureByName("Serious"), evs);
  REQUIRE(s.hp == 298);
  REQUIRE(s.specAtk == 317);
  REQUIRE(s.speed == 299);
}

TEST_CASE("Nature applies +10%/-10% after the flat formula", "[stats]") {
  Stats evs{};
  evs.speed = 252;
  // Timid: +Spe / -Atk
  Stats s = computeStats(kBase, 100, *natureByName("Timid"), evs);
  REQUIRE(s.speed == 328);
  REQUIRE(s.atk == 183);
  REQUIRE(s.specAtk == 254);
  REQUIRE(s.hp == 297);
}

TEST_CASE("The nature table knows all 25 canon French names", "[stats]") {
  for (const char *n : {"Hardy",  "Docile", "Serious", "Bashful", "Quirky",  "Adamant", "Bold",
                        "Modest", "Calm",   "Jolly",   "Timid",   "Careful", "Brave",   "Relaxed",
                        "Quiet",  "Sassy",  "Naive",   "Lonely",  "Naughty", "Mild",    "Rash",
                        "Gentle", "Impish", "Lax",     "Hasty"})
    REQUIRE(natureByName(n) != nullptr);
  REQUIRE(natureByName("Brave2") == nullptr);
  REQUIRE(natureByName("") == nullptr);
}

TEST_CASE("Neutral natures leave every stat untouched", "[stats]") {
  Stats a = computeStats(kBase, 100, *natureByName("Serious"), Stats{});
  Stats b = computeStats(kBase, 100, *natureByName("Bashful"), Stats{});
  REQUIRE(a.atk == b.atk);
  REQUIRE(a.speed == b.speed);
}
