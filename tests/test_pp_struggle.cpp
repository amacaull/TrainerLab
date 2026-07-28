#include "helpers.hpp"

#include "engine/core/battle_state.hpp"
#include "engine/core/data_loader.hpp"
#include "engine/core/engine.hpp"
#include "engine/core/rng.hpp"
#include "engine/core/validate.hpp"
#include "engine/model/status.hpp"

#include <catch2/catch_test_macros.hpp>

#include <stdexcept>
#include <variant>

using namespace engine;
using engine::test::buildCombatant;

namespace {

template <typename E> int countEvents(const EventLog &events) {
  int n = 0;
  for (const auto &ev : events)
    if (std::holds_alternative<E>(ev))
      ++n;
  return n;
}

int damageOn(const EventLog &events, int side) {
  for (const auto &ev : events)
    if (auto *e = std::get_if<DamageDealtEvent>(&ev))
      if (e->target.side == side)
        return e->damage;
  return -1;
}

// chancePct always fails (accuracy rolls miss), chance() never procs.
class MissRNG : public RNG {
public:
  int rangeInt(int /*min*/, int max) override { return max; }
  float unit() override { return 0.99f; }
};

BattleState makeDuel(const DataLoader &data, const char *s0, std::vector<std::string> m0,
                     const char *s1, std::vector<std::string> m1) {
  BattleState state;
  state.teams[0][0] = buildCombatant(data, s0, 100, m0);
  state.teams[1][0] = buildCombatant(data, s1, 100, m1);
  state.team_size = {1, 1};
  return state;
}

} // namespace

TEST_CASE("PP is loaded from the move catalog and billed on execution", "[phase10][pp]") {
  DataLoader data;
  engine::test::loadAll(data);
  BattleEngine engine(data);

  auto state = makeDuel(data, "Snorlax", {"Tackle"}, "Conkeldurr", {"Growl"});
  REQUIRE(state.teams[0][0].pp[0] == 35);
  REQUIRE(state.teams[1][0].pp[0] == 40);

  FixedRNG rng(0.99f);
  engine.resolveTurn(state, UseMove{0}, UseMove{0}, rng);
  REQUIRE(state.teams[0][0].pp[0] == 34);
  REQUIRE(state.teams[1][0].pp[0] == 39);
}

TEST_CASE("A skipped turn doesn't pay PP", "[phase10][pp]") {
  DataLoader data;
  engine::test::loadAll(data);
  BattleEngine engine(data);

  auto state = makeDuel(data, "Snorlax", {"Tackle"}, "Conkeldurr", {"Growl"});
  state.teams[0][0].status = Status::Sleep;
  state.teams[0][0].status_turns = 2;

  FixedRNG rng(0.99f);
  auto events = engine.resolveTurn(state, UseMove{0}, UseMove{0}, rng);
  REQUIRE(countEvents<MoveSkippedEvent>(events) == 1);
  REQUIRE(state.teams[0][0].pp[0] == 35);
  REQUIRE(state.teams[1][0].pp[0] == 39);
}

TEST_CASE("A failed move still pays its PP", "[phase10][pp]") {
  DataLoader data;
  engine::test::loadAll(data);
  BattleEngine engine(data);

  // Recover at full HP fails, but the PP is spent (canon).
  auto state = makeDuel(data, "MegaGengar", {"Recover"}, "Snorlax", {"Growl"});
  FixedRNG rng(0.99f);
  auto events = engine.resolveTurn(state, UseMove{0}, UseMove{0}, rng);
  REQUIRE(countEvents<MoveFailedEvent>(events) == 1);
  REQUIRE(state.teams[0][0].pp[0] == 4);
}

TEST_CASE("A missed move still pays its PP", "[phase10][pp]") {
  DataLoader data;
  engine::test::loadAll(data);
  BattleEngine engine(data);

  auto state = makeDuel(data, "Conkeldurr", {"Growl"}, "Snorlax", {"Tackle"});
  state.teams[1][0].stat_stages[static_cast<size_t>(StatIndex::Evasion)] = 6;

  MissRNG rng;
  auto events = engine.resolveTurn(state, UseMove{0}, UseMove{0}, rng);
  REQUIRE(countEvents<MissedEvent>(events) >= 1);
  REQUIRE(state.teams[0][0].pp[0] == 39);
}

TEST_CASE("Two-turn moves pay on the charge turn only", "[phase10][pp]") {
  DataLoader data;
  engine::test::loadAll(data);
  BattleEngine engine(data);

  auto state = makeDuel(data, "Infernape", {"Fly"}, "Snorlax", {"Growl"});
  FixedRNG rng(0.99f);

  auto t1 = engine.resolveTurn(state, UseMove{0}, UseMove{0}, rng);
  REQUIRE(countEvents<ChargingEvent>(t1) == 1);
  REQUIRE(state.teams[0][0].pp[0] == 14);

  engine.resolveTurn(state, UseMove{0}, UseMove{0}, rng);
  REQUIRE(state.teams[0][0].pp[0] == 14);
}

TEST_CASE("Out of PP everywhere: the engine substitutes Struggle", "[phase10][struggle]") {
  DataLoader data;
  engine::test::loadAll(data);
  BattleEngine engine(data);

  // Normal vs Ghost: Tackle would bounce off Gengar (0x). Struggle is typeless
  // and connects for neutral damage, with a flat 25% max-HP recoil.
  auto state = makeDuel(data, "Snorlax", {"Tackle"}, "MegaGengar", {"SwordsDance"});
  state.teams[0][0].pp[0] = 0;

  FixedRNG rng(0.99f);
  auto events = engine.resolveTurn(state, UseMove{0}, UseMove{0}, rng);

  bool usedStruggle = false;
  for (const auto &ev : events)
    if (auto *e = std::get_if<MoveUsedEvent>(&ev))
      if (e->user.side == 0 && e->moveName == "Struggle")
        usedStruggle = true;
  REQUIRE(usedStruggle);

  REQUIRE(damageOn(events, 1) > 0);
  REQUIRE(countEvents<RecoilDamageEvent>(events) == 1);
  int maxHp = state.teams[0][0].stats.hp;
  REQUIRE(state.teams[0][0].currentHp == maxHp - maxHp / 4);
  REQUIRE(state.teams[0][0].pp[0] == 0);
}

TEST_CASE("checkAction rejects an empty-PP slot while another slot has PP", "[phase10][struggle]") {
  DataLoader data;
  engine::test::loadAll(data);
  BattleEngine engine(data);

  auto state = makeDuel(data, "Snorlax", {"Tackle", "BodySlam"}, "Conkeldurr", {"Growl"});
  state.teams[0][0].pp[0] = 0;

  FixedRNG rng(0.99f);
  REQUIRE_THROWS_AS(engine.resolveTurn(state, UseMove{0}, UseMove{0}, rng), std::invalid_argument);
  REQUIRE_NOTHROW(engine.resolveTurn(state, UseMove{1}, UseMove{0}, rng));
}

TEST_CASE("validateState checks the PP invariants", "[phase10][validate]") {
  DataLoader data;
  engine::test::loadAll(data);

  auto state = makeDuel(data, "Snorlax", {"Tackle"}, "Conkeldurr", {"Growl"});
  REQUIRE_NOTHROW(validateState(state, data));

  state.teams[0][0].pp[0] = 99; // above Tackle's max (35)
  REQUIRE_THROWS_AS(validateState(state, data), std::invalid_argument);
  state.teams[0][0].pp[0] = -1;
  REQUIRE_THROWS_AS(validateState(state, data), std::invalid_argument);
  state.teams[0][0].pp[0] = 35;

  state.teams[0][0].pp[3] = 5; // PP on an empty slot
  REQUIRE_THROWS_AS(validateState(state, data), std::invalid_argument);
}

TEST_CASE("6v6: full teams are valid and slot 5 is reachable", "[phase10][6v6]") {
  DataLoader data;
  engine::test::loadAll(data);
  BattleEngine engine(data);
  REQUIRE(kTeamSize == 6);

  const char *roster[6] = {"Snorlax",   "Conkeldurr", "MegaGengar",
                           "Infernape", "Toxapex",    "Inteleon"};
  BattleState state;
  for (int side = 0; side < 2; ++side)
    for (int i = 0; i < 6; ++i)
      state.teams[static_cast<size_t>(side)][static_cast<size_t>(i)] =
          buildCombatant(data, roster[i], 100, {"Tackle"});
  state.team_size = {6, 6};
  REQUIRE_NOTHROW(validateState(state, data));

  FixedRNG rng(0.99f);
  engine.resolveTurn(state, SwitchAction{5}, UseMove{0}, rng);
  REQUIRE(state.activeIndex[0] == 5);
}

TEST_CASE("Species stats come from the locked nature and EVs", "[phase10][stats]") {
  DataLoader data;
  engine::test::loadAll(data);

  const Species &sp = data.speciesByIndex(data.findSpeciesId("Infernape"));
  REQUIRE(sp.nature == "Jolly"); // roster sheet: +Spe / -SpA
  Stats s = computeSpeciesStats(sp, 100);
  REQUIRE(s.hp == 293);
  REQUIRE(s.specAtk == 219); // 244 lowered by the nature
  REQUIRE(s.speed == 346);   // 315 raised by the nature
  REQUIRE(sp.weightKg == 55.0);
}
