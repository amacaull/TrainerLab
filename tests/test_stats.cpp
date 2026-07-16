#include "engine/model/stats.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace engine;

// Charizard base stats; reference values cross-checked with Showdown's
// damage calc (IV 31, level 100).
static const Stats kBase{78, 84, 78, 109, 85, 100};

TEST_CASE("Stats at level 100, neutral nature, no EVs", "[stats]") {
  Stats s = computeStats(kBase, 100, *natureByName("Sérieux"), Stats{});
  REQUIRE(s.hp == 297);
  REQUIRE(s.atk == 204);
  REQUIRE(s.def == 192);
  REQUIRE(s.specAtk == 254);
  REQUIRE(s.specDef == 206);
  REQUIRE(s.speed == 236);
}

TEST_CASE("Stats at level 50 still follow the canon formula", "[stats]") {
  Stats s = computeStats(kBase, 50, *natureByName("Sérieux"), Stats{});
  REQUIRE(s.hp == 153);
  REQUIRE(s.atk == 104);
  REQUIRE(s.speed == 120);
}

TEST_CASE("EVs feed the formula (floor(ev/4))", "[stats]") {
  Stats evs{};
  evs.speed = 252;
  evs.specAtk = 252;
  evs.hp = 4;
  Stats s = computeStats(kBase, 100, *natureByName("Sérieux"), evs);
  REQUIRE(s.hp == 298);      // +floor(4/4)
  REQUIRE(s.specAtk == 317); // +63
  REQUIRE(s.speed == 299);   // +63
}

TEST_CASE("Nature applies +10%/-10% after the flat formula", "[stats]") {
  Stats evs{};
  evs.speed = 252;
  // Timide : +Vit / -Atk
  Stats s = computeStats(kBase, 100, *natureByName("Timide"), evs);
  REQUIRE(s.speed == 328); // floor(299 * 1.1)
  REQUIRE(s.atk == 183);   // floor(204 * 0.9)
  REQUIRE(s.specAtk == 254);
  REQUIRE(s.hp == 297); // HP never takes a nature
}

TEST_CASE("The nature table knows all 25 canon French names", "[stats]") {
  // One per family + the five neutrals.
  for (const char *n : {"Hardi",   "Docile",  "Sérieux", "Pudique", "Bizarre", "Rigide", "Assuré",
                        "Modeste", "Calme",   "Jovial",  "Timide",  "Prudent", "Brave",  "Relax",
                        "Discret", "Malpoli", "Naïf",    "Solo",    "Mauvais", "Doux",   "Foufou",
                        "Gentil",  "Malin",   "Lâche",   "Pressé"})
    REQUIRE(natureByName(n) != nullptr);
  REQUIRE(natureByName("Adamant") == nullptr);
  REQUIRE(natureByName("") == nullptr);
}

TEST_CASE("Neutral natures leave every stat untouched", "[stats]") {
  Stats a = computeStats(kBase, 100, *natureByName("Sérieux"), Stats{});
  Stats b = computeStats(kBase, 100, *natureByName("Pudique"), Stats{});
  REQUIRE(a.atk == b.atk);
  REQUIRE(a.speed == b.speed);
}
