#include "helpers.hpp"

#include "engine/core/battle_state.hpp"
#include "engine/core/data_loader.hpp"
#include "engine/core/engine.hpp"
#include "engine/core/outcome_walker.hpp"
#include "engine/core/rng.hpp"
#include "engine/ffi/ffi.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <map>
#include <stdexcept>
#include <string>
#include <vector>

using namespace engine;
using engine::test::buildCombatant;

namespace {
// Forces one outcome: every check passes, the crit is as asked, the roll is as asked.
class ScriptedRNG : public RNG {
public:
  ScriptedRNG(int roll, bool crit) : roll_(roll), crit_(crit) {}
  int rangeInt(int min, int /*max*/) override { return min; }
  float unit() override { return 0.99f; }
  bool chance(float /*p*/) override { return crit_; }
  int damageRoll() override { return roll_; }

private:
  int roll_;
  bool crit_;
};

// Snorlax's Tackle on a Blissey that moves first and whose Soft-Boiled fails at full HP: the turn
// is the Tackle alone.
struct TackleDuel {
  DataLoader data;
  BattleState state;
  TackleDuel() {
    engine::test::loadAll(data);
    state.teams[0][0] = buildCombatant(data, "Snorlax", {"Tackle"});
    state.teams[1][0] = buildCombatant(data, "Blissey", {"SoftBoiled"});
    state.team_size = {1, 1};
  }

  int dealt(RNG &rng) const {
    BattleState s = state;
    BattleEngine(data).resolveTurn(s, UseMove{0}, UseMove{0}, rng);
    return s.teams[1][0].stats.hp - s.teams[1][0].currentHp;
  }
};
} // namespace

TEST_CASE("the walker visits every sequence of draws once, with its probability",
          "[expected_damage]") {
  OutcomeWalker walker;
  std::map<std::vector<int>, double> seen;
  do {
    std::vector<int> draws;
    draws.push_back(walker.chance(0.25f) ? 1 : 0);
    if (draws.back() == 1)
      draws.push_back(walker.rangeInt(1, 3));
    draws.push_back(walker.bucket(1, 100, {35, 70, 85}));
    draws.push_back(walker.chancePct(100) ? 1 : 0); // certain: not a branch
    REQUIRE(seen.count(draws) == 0);
    seen[draws] = walker.weight();
  } while (walker.next());

  REQUIRE(seen.size() == (1 + 3) * 4);
  double total = 0.0;
  for (const auto &[draws, weight] : seen)
    total += weight;
  REQUIRE(total == Catch::Approx(1.0));
  REQUIRE(seen.at({1, 2, 0, 1}) == Catch::Approx(0.25 / 3 * 0.35));
  REQUIRE(seen.at({0, 3, 1}) == Catch::Approx(0.75 * 0.15));
  REQUIRE_THROWS_AS(walker.unit(), std::logic_error);
}

TEST_CASE("a single hit: min, max and mean are those of the real damage rolls",
          "[expected_damage]") {
  TackleDuel duel;
  const DamageOutlook o = BattleEngine(duel.data).expectedDamage(duel.state, 0, 0);

  double mean = 0.0;
  for (int roll = 85; roll <= 100; ++roll) {
    ScriptedRNG normal(roll, false), crit(roll, true);
    mean += (23.0 / 24.0 * duel.dealt(normal) + 1.0 / 24.0 * duel.dealt(crit)) / 16.0;
  }
  ScriptedRNG lowest(85, false), highest(100, true);
  REQUIRE(o.exact);
  REQUIRE(o.min == duel.dealt(lowest));
  REQUIRE(o.max == duel.dealt(highest));
  REQUIRE(o.mean == Catch::Approx(mean));
  REQUIRE(o.min < o.max);
}

TEST_CASE("the outlook leaves the state untouched and gives the same answer twice",
          "[expected_damage]") {
  TackleDuel duel;
  const BattleState before = duel.state;
  BattleEngine engine(duel.data);
  const DamageOutlook a = engine.expectedDamage(duel.state, 0, 0);
  const DamageOutlook b = engine.expectedDamage(duel.state, 0, 0);
  REQUIRE(duel.state == before);
  REQUIRE(a.min == b.min);
  REQUIRE(a.max == b.max);
  REQUIRE(a.mean == b.mean);
}

TEST_CASE("full paralysis takes a quarter of the expected damage", "[expected_damage]") {
  TackleDuel duel;
  BattleEngine engine(duel.data);
  const double healthy = engine.expectedDamage(duel.state, 0, 0).mean;
  duel.state.teams[0][0].status = Status::Paralysis;
  const DamageOutlook paralysed = engine.expectedDamage(duel.state, 0, 0);
  REQUIRE(paralysed.mean == Catch::Approx(0.75 * healthy));
  REQUIRE(paralysed.min == 0);
}

TEST_CASE("a fainted active, on either side, expects nothing", "[expected_damage]") {
  TackleDuel duel;
  BattleEngine engine(duel.data);
  duel.state.teams[1][0].currentHp = 0;
  const DamageOutlook o = engine.expectedDamage(duel.state, 0, 0);
  REQUIRE(o.max == 0);
  REQUIRE(o.mean == 0.0);
  REQUIRE(engine.expectedDamage(duel.state, 1, 0).max == 0);
}

// ---------------------------------------------------------------------------------------------
// Through the flat API, on the shipped catalog and the real sets

namespace {
using namespace engine::ffi;

void ensureInit() { engine_init(BATTLE_ENGINE_DATA_DIR); }

BattleState duel(const char *left, const char *right) {
  ensureInit();
  BattleState s;
  s.teams[0][0] = make_combatant(find_species_id(left));
  s.teams[1][0] = make_combatant(find_species_id(right));
  s.team_size = {1, 1};
  return s;
}

int slotOf(const BattlePokemon &p, const char *move) {
  const int id = find_move_id(move);
  for (int i = 0; i < kMaxMovesPerPokemon; ++i)
    if (p.move_ids[static_cast<size_t>(i)] == id)
      return i;
  FAIL("move " << move << " not in the set");
  return -1;
}

FfiDamage outlook(const BattleState &s, const char *move, int side = 0) {
  return expected_damage(s, side, slotOf(s.active(side), move));
}

template <typename F> std::string messageOf(F &&f) {
  try {
    f();
  } catch (const std::exception &e) {
    return e.what();
  }
  return {};
}
} // namespace

TEST_CASE("fixed damage: Seismic Toss takes exactly the level, and nothing from a Ghost",
          "[expected_damage][ffi]") {
  const FfiDamage onSnorlax = outlook(duel("Blissey", "Snorlax"), "SeismicToss");
  REQUIRE(onSnorlax.min == 100);
  REQUIRE(onSnorlax.max == 100);
  REQUIRE(onSnorlax.mean == 100.0);
  REQUIRE(onSnorlax.exact);
  REQUIRE(outlook(duel("Blissey", "MegaGengar"), "SeismicToss").max == 0);
}

TEST_CASE("an immunity granted by an ability is no damage, and a heal is not negative",
          "[expected_damage][ffi]") {
  REQUIRE(outlook(duel("Excadrill", "WeezingGalar"), "Earthquake").max == 0); // Levitate
  BattleState s = duel("TapuKoko", "ThundurusT");                             // Volt Absorb
  s.teams[1][0].currentHp = s.teams[1][0].stats.hp / 2;
  const FfiDamage absorbed = outlook(s, "Thunder");
  REQUIRE(absorbed.min == 0);
  REQUIRE(absorbed.max == 0);
}

TEST_CASE("moves that fail in this position expect nothing", "[expected_damage][ffi]") {
  SECTION("Fake Out works on the first turn only") {
    BattleState s = duel("Maushold", "Snorlax");
    REQUIRE(outlook(s, "FakeOut").min > 0);
    s.teams[0][0].turns_on_field = 1;
    REQUIRE(outlook(s, "FakeOut").max == 0);
  }
  SECTION("Poltergeist needs an item on the target") {
    BattleState s = duel("MegaBanette", "Corviknight");
    REQUIRE(outlook(s, "Poltergeist").max > 0);
    s.teams[1][0].item_id = kNoItem;
    REQUIRE(outlook(s, "Poltergeist").max == 0);
  }
  SECTION("a status move takes no HP") {
    REQUIRE(outlook(duel("Quagsire", "Snorlax"), "Toxic").max == 0);
  }
}

TEST_CASE("accuracy: a miss is an outcome", "[expected_damage][ffi]") {
  BattleState s = duel("Inteleon", "Snorlax"); // Hydro Pump, 80 %
  const FfiDamage normal = outlook(s, "HydroPump");
  s.teams[0][0].stat_stages[static_cast<size_t>(StatIndex::Accuracy)] = 6;
  const FfiDamage sure = outlook(s, "HydroPump");
  REQUIRE(normal.min == 0);
  REQUIRE(normal.max == sure.max);
  REQUIRE(sure.min > 0);
  REQUIRE(normal.mean == Catch::Approx(0.8 * sure.mean));
}

TEST_CASE("multi-hit moves count every hit", "[expected_damage][ffi]") {
  SECTION("two fixed hits are walked exactly") {
    const BattleState s = duel("Dragapult", "Snorlax");
    const FfiDamage darts = outlook(s, "DragonDarts");
    REQUIRE(darts.exact);
    REQUIRE(darts.min > 0);
  }
  SECTION("Triple Axel is three hits of rising power, sampled") {
    const FfiDamage axel = outlook(duel("Weavile", "Snorlax"), "TripleAxel");
    REQUIRE_FALSE(axel.exact);
    REQUIRE(axel.min == 0); // first hit missed
    REQUIRE(axel.mean > 0.0);
    REQUIRE(axel.max > axel.mean);
  }
  SECTION("a sampled outlook is still the same every time") {
    const BattleState s = duel("Maushold", "Snorlax");
    const FfiDamage a = outlook(s, "BulletSeed");
    const FfiDamage b = outlook(s, "BulletSeed");
    REQUIRE_FALSE(a.exact);
    REQUIRE(a.mean == b.mean);
    REQUIRE(a.min == b.min);
    REQUIRE(a.max == b.max);
  }
}

TEST_CASE("a two-turn move counts the damage of its release", "[expected_damage][ffi]") {
  const FfiDamage phantom = outlook(duel("Dragapult", "Corviknight"), "PhantomForce");
  REQUIRE(phantom.min > 0);
}

TEST_CASE("Sucker Punch counts when the foe has a damaging move", "[expected_damage][ffi]") {
  REQUIRE(outlook(duel("MegaMawile", "Snorlax"), "SuckerPunch").min > 0);
  BattleState s = duel("MegaMawile", "Snorlax");
  s.teams[1][0].move_ids = {find_move_id("Curse"), find_move_id("Rest"), kNoMove, kNoMove};
  s.teams[1][0].pp = {10, 5, 0, 0};
  REQUIRE(outlook(s, "SuckerPunch").max == 0);
}

TEST_CASE("a player's view is accepted, and gives the same outlook when nothing is hidden",
          "[expected_damage][ffi]") {
  ensureInit();
  BattleState s;
  const char *teams[2][3] = {{"Excadrill", "Blissey", "Toxapex"},
                             {"Snorlax", "Corviknight", "Gyarados"}};
  for (size_t side = 0; side < 2; ++side) {
    for (size_t i = 0; i < 3; ++i)
      s.teams[side][i] = make_combatant(find_species_id(teams[side][i]));
    s.team_size[side] = 3;
  }
  start_battle(s, 1);
  const BattleState view = observe(s, 0);
  REQUIRE(view.teams[1][1].species_id == kNoSpecies);
  REQUIRE_THROWS(validate_state(view));
  for (int slot = 0; slot < kMaxMovesPerPokemon; ++slot) {
    const FfiDamage real = expected_damage(s, 0, slot);
    const FfiDamage seen = expected_damage(view, 0, slot);
    REQUIRE(seen.min == real.min);
    REQUIRE(seen.max == real.max);
    REQUIRE(seen.mean == real.mean);
  }
}

TEST_CASE("expected_damage refuses a bad side, slot or state", "[expected_damage][ffi]") {
  BattleState s = duel("Blissey", "Snorlax");
  REQUIRE(messageOf([&] { expected_damage(s, 2, 0); }).rfind("E_ARG", 0) == 0);
  REQUIRE(messageOf([&] { expected_damage(s, 0, 4); }).rfind("E_ARG", 0) == 0);
  s.teams[0][0].move_ids[3] = kNoMove;
  s.teams[0][0].pp[3] = 0;
  REQUIRE(messageOf([&] { expected_damage(s, 0, 3); }).rfind("E_ARG", 0) == 0);
  BattleState hidden = duel("Blissey", "Snorlax");
  hidden.teams[1][0] = BattlePokemon{}; // an active is never hidden
  REQUIRE(messageOf([&] { expected_damage(hidden, 0, 0); }).rfind("E_STATE", 0) == 0);
}

TEST_CASE("every move of every species against every species stays within bounds",
          "[expected_damage][ffi]") {
  ensureInit();
  int sampled = 0;
  for (int a = 0; a < species_count(); ++a) {
    for (int d = 0; d < species_count(); ++d) {
      if (a == d)
        continue;
      BattleState s;
      s.teams[0][0] = make_combatant(a);
      s.teams[1][0] = make_combatant(d);
      s.team_size = {1, 1};
      for (int slot = 0; slot < kMaxMovesPerPokemon; ++slot) {
        const FfiDamage o = expected_damage(s, 0, slot);
        INFO(species_id_string(a) << " " << move_name(s.teams[0][0].move_ids[size_t(slot)])
                                  << " on " << species_id_string(d));
        REQUIRE(0 <= o.min);
        REQUIRE(o.min <= o.mean + 1e-9);
        REQUIRE(o.mean <= o.max + 1e-9);
        REQUIRE(o.max <= s.teams[1][0].stats.hp);
        sampled += o.exact ? 0 : 1;
      }
    }
  }
  // Only the volleys of several hits each with a crit and a roll are sampled.
  REQUIRE(sampled > 0);
  REQUIRE(sampled < 5 * species_count());
}
