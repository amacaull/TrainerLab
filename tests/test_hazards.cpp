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

// Side 0: setter + bench; side 1: entrant on a mined field via replacement.
BattleState makeHazardField(const DataLoader &data, const char *entrant) {
  BattleState state;
  state.teams[0][0] = buildCombatant(data, "snorlax", 100, {"Growl"});
  state.teams[1][0] = buildCombatant(data, "machamp", 100, {"CloseCombat"});
  state.teams[1][1] = buildCombatant(data, entrant, 50, {"Tackle"});
  state.team_size = {1, 2};
  return state;
}

// Damage taken by side 1's entrant when switching onto the given hazards.
int entryDamage(const DataLoader &data, const BattleEngine &engine, const char *entrant,
                SideHazards hazards, Status *statusOut = nullptr) {
  auto state = makeHazardField(data, entrant);
  state.hazards[1] = hazards;
  int before = state.teams[1][1].currentHp;
  FixedRNG rng(0.5f);
  engine.resolveTurn(state, UseMove{0}, SwitchAction{1}, rng);
  if (statusOut)
    *statusOut = state.teams[1][1].status;
  return before - state.teams[1][1].currentHp;
}

} // namespace

TEST_CASE("StealthRock sets once on the opposing side then fails", "[hazard]") {
  DataLoader data;
  data.loadAll(BATTLE_ENGINE_DATA_DIR);
  BattleEngine engine(data);

  BattleState state;
  state.teams[0][0] = buildCombatant(data, "tyranitar", 100, {"StealthRock"});
  state.teams[1][0] = buildCombatant(data, "snorlax", 100, {"Tackle"});
  state.team_size = {1, 1};

  FixedRNG rng(0.5f);
  auto t1 = engine.resolveTurn(state, UseMove{0}, UseMove{0}, rng);
  REQUIRE(countEvents<HazardSetEvent>(t1) == 1);
  REQUIRE(state.hazards[1].stealth_rock == 1);
  REQUIRE(state.hazards[0].stealth_rock == 0); // opposing side only

  auto t2 = engine.resolveTurn(state, UseMove{0}, UseMove{0}, rng);
  REQUIRE(countEvents<MoveFailedEvent>(t2) == 1);
  REQUIRE(state.hazards[1].stealth_rock == 1);
}

TEST_CASE("StealthRock entry damage scales with Rock effectiveness", "[hazard]") {
  DataLoader data;
  data.loadAll(BATTLE_ENGINE_DATA_DIR);
  BattleEngine engine(data);

  SideHazards sr{1, 0, 0};

  // Neutral: 1/8. Charizard (Fire/Flying): 4x -> 1/2. Machamp (Fighting): 1/16.
  auto state = makeHazardField(data, "snorlax");
  int snorlaxMax = state.teams[1][1].stats.hp;
  REQUIRE(entryDamage(data, engine, "snorlax", sr) == snorlaxMax / 8);

  auto stateC = makeHazardField(data, "charizard");
  int chariMax = stateC.teams[1][1].stats.hp;
  REQUIRE(entryDamage(data, engine, "charizard", sr) == chariMax / 2);

  auto stateM = makeHazardField(data, "machamp");
  int machMax = stateM.teams[1][1].stats.hp;
  REQUIRE(entryDamage(data, engine, "machamp", sr) == machMax / 16);

  // Levitate does not dodge rocks.
  REQUIRE(entryDamage(data, engine, "gengar", sr) > 0);
}

TEST_CASE("Spikes damage ramps with layers and caps at three", "[hazard]") {
  DataLoader data;
  data.loadAll(BATTLE_ENGINE_DATA_DIR);
  BattleEngine engine(data);

  auto state = makeHazardField(data, "snorlax");
  int maxHp = state.teams[1][1].stats.hp;

  REQUIRE(entryDamage(data, engine, "snorlax", SideHazards{0, 1, 0}) == maxHp / 8);
  REQUIRE(entryDamage(data, engine, "snorlax", SideHazards{0, 2, 0}) == maxHp / 6);
  REQUIRE(entryDamage(data, engine, "snorlax", SideHazards{0, 3, 0}) == maxHp / 4);
}

TEST_CASE("Flying types and Levitate ignore Spikes and Toxic Spikes", "[hazard]") {
  DataLoader data;
  data.loadAll(BATTLE_ENGINE_DATA_DIR);
  BattleEngine engine(data);

  Status st = Status::None;
  REQUIRE(entryDamage(data, engine, "charizard", SideHazards{0, 3, 0}) == 0);
  REQUIRE(entryDamage(data, engine, "gengar", SideHazards{0, 3, 2}, &st) == 0);
  REQUIRE(st == Status::None); // Levitate: no Toxic Spikes poison either

  // ...and a floating Poison type does NOT absorb them.
  auto state = makeHazardField(data, "gengar");
  state.hazards[1] = SideHazards{0, 0, 2};
  FixedRNG rng(0.5f);
  engine.resolveTurn(state, UseMove{0}, SwitchAction{1}, rng);
  REQUIRE(state.hazards[1].toxic_spikes == 2);
}

TEST_CASE("Toxic Spikes: one layer poisons, two badly poison", "[hazard][status]") {
  DataLoader data;
  data.loadAll(BATTLE_ENGINE_DATA_DIR);
  BattleEngine engine(data);

  Status st = Status::None;
  entryDamage(data, engine, "snorlax", SideHazards{0, 0, 1}, &st);
  REQUIRE(st == Status::Poison);

  entryDamage(data, engine, "snorlax", SideHazards{0, 0, 2}, &st);
  REQUIRE(st == Status::Toxic);
}

TEST_CASE("A grounded Poison type absorbs Toxic Spikes", "[hazard][status]") {
  DataLoader data;
  data.loadAll(BATTLE_ENGINE_DATA_DIR);
  BattleEngine engine(data);

  auto state = makeHazardField(data, "venusaur");
  state.hazards[1] = SideHazards{0, 0, 2};
  FixedRNG rng(0.5f);
  auto events = engine.resolveTurn(state, UseMove{0}, SwitchAction{1}, rng);

  REQUIRE(countEvents<ToxicSpikesAbsorbedEvent>(events) == 1);
  REQUIRE(state.hazards[1].toxic_spikes == 0);
  REQUIRE(state.teams[1][1].status == Status::None);
}

TEST_CASE("Scizor (grounded Steel) takes Spikes but shrugs off Toxic Spikes", "[hazard][status]") {
  DataLoader data;
  data.loadAll(BATTLE_ENGINE_DATA_DIR);
  BattleEngine engine(data);

  Status st = Status::None;
  int dmg = entryDamage(data, engine, "scizor", SideHazards{0, 1, 2}, &st);
  REQUIRE(dmg > 0);            // Spikes land
  REQUIRE(st == Status::None); // Steel can't be poisoned
}

TEST_CASE("Hazards apply on KO replacements too", "[hazard][replacement]") {
  DataLoader data;
  data.loadAll(BATTLE_ENGINE_DATA_DIR);
  BattleEngine engine(data);

  auto state = makeHazardField(data, "snorlax");
  state.hazards[1] = SideHazards{1, 0, 0};
  state.teams[1][0].currentHp = 0;

  int before = state.teams[1][1].currentHp;
  auto events = engine.resolveReplacement(state, 1, 1);

  REQUIRE(countEvents<HazardDamageEvent>(events) == 1);
  REQUIRE(state.teams[1][1].currentHp == before - state.teams[1][1].stats.hp / 8);
}

TEST_CASE("Fainting to hazards on entry suppresses the switch-in ability", "[hazard][ability]") {
  DataLoader data;
  data.loadAll(BATTLE_ENGINE_DATA_DIR);
  BattleEngine engine(data);

  auto state = makeHazardField(data, "gyarados");
  state.hazards[1] = SideHazards{1, 0, 0};
  state.teams[1][1].currentHp = 5; // 4x rocks will finish it

  FixedRNG rng(0.5f);
  auto events = engine.resolveTurn(state, UseMove{0}, SwitchAction{1}, rng);

  REQUIRE(countEvents<FaintedEvent>(events) == 1);
  REQUIRE(countEvents<AbilityTriggeredEvent>(events) == 0); // no Intimidate
  REQUIRE(state.teams[0][0].stat_stages[static_cast<size_t>(StatIndex::Atk)] == 0);
}

TEST_CASE("RapidSpin clears the user's side only", "[hazard]") {
  DataLoader data;
  data.loadAll(BATTLE_ENGINE_DATA_DIR);
  BattleEngine engine(data);

  BattleState state;
  state.teams[0][0] = buildCombatant(data, "blastoise", 100, {"RapidSpin"});
  state.teams[1][0] = buildCombatant(data, "snorlax", 100, {"Tackle"});
  state.team_size = {1, 1};
  state.hazards[0] = SideHazards{1, 2, 1};
  state.hazards[1] = SideHazards{1, 0, 0};

  FixedRNG rng(0.5f);
  auto events = engine.resolveTurn(state, UseMove{0}, UseMove{0}, rng);

  REQUIRE(countEvents<HazardsClearedEvent>(events) == 1);
  REQUIRE(state.hazards[0].stealth_rock == 0);
  REQUIRE(state.hazards[0].spikes == 0);
  REQUIRE(state.hazards[0].toxic_spikes == 0);
  REQUIRE(state.hazards[1].stealth_rock == 1); // opponent keeps theirs
}

TEST_CASE("A Ghost blocks RapidSpin: no damage, no removal", "[hazard][immunity]") {
  DataLoader data;
  data.loadAll(BATTLE_ENGINE_DATA_DIR);
  BattleEngine engine(data);

  BattleState state;
  state.teams[0][0] = buildCombatant(data, "blastoise", 100, {"RapidSpin"});
  state.teams[1][0] = buildCombatant(data, "gengar", 100, {"ShadowBall"});
  state.team_size = {1, 1};
  state.hazards[0] = SideHazards{1, 0, 0};

  FixedRNG rng(0.5f);
  auto events = engine.resolveTurn(state, UseMove{0}, UseMove{0}, rng);

  REQUIRE(countEvents<HazardsClearedEvent>(events) == 0);
  REQUIRE(state.hazards[0].stealth_rock == 1);
}

TEST_CASE("VoltSwitch against a Ground type fails and does not pivot", "[hazard][immunity]") {
  DataLoader data;
  data.loadAll(BATTLE_ENGINE_DATA_DIR);
  BattleEngine engine(data);

  BattleState state;
  state.teams[0][0] = buildCombatant(data, "pikachu", 100, {"VoltSwitch"});
  state.teams[0][1] = buildCombatant(data, "snorlax", 100, {"Tackle"});
  state.teams[1][0] = buildCombatant(data, "garchomp", 100, {"DragonClaw"});
  state.team_size = {2, 1};

  FixedRNG rng(0.5f);
  auto events = engine.resolveTurn(state, UseMove{0, 1}, UseMove{0}, rng);

  REQUIRE(countEvents<SwitchedOutEvent>(events) == 0);
  REQUIRE(state.activeIndex[0] == 0); // Pikachu stayed in (canon)
}

TEST_CASE("Defog clears both sides and stores the Evasion drop", "[hazard]") {
  DataLoader data;
  data.loadAll(BATTLE_ENGINE_DATA_DIR);
  BattleEngine engine(data);

  BattleState state;
  state.teams[0][0] = buildCombatant(data, "scizor", 100, {"Defog"});
  state.teams[1][0] = buildCombatant(data, "snorlax", 100, {"Tackle"});
  state.team_size = {1, 1};
  state.hazards[0] = SideHazards{1, 3, 0};
  state.hazards[1] = SideHazards{1, 0, 2};

  FixedRNG rng(0.5f);
  auto events = engine.resolveTurn(state, UseMove{0}, UseMove{0}, rng);

  REQUIRE(countEvents<HazardsClearedEvent>(events) == 2);
  REQUIRE(state.hazards[0].spikes == 0);
  REQUIRE(state.hazards[1].toxic_spikes == 0);
  // Evasion -1 stored on the target, inert until phase 8 (ADR #18).
  REQUIRE(state.teams[1][0].stat_stages[static_cast<size_t>(StatIndex::Evasion)] == -1);
}

TEST_CASE("validateState checks hazard layer bounds", "[hazard][validate]") {
  DataLoader data;
  data.loadAll(BATTLE_ENGINE_DATA_DIR);

  BattleState state;
  state.teams[0][0] = buildCombatant(data, "snorlax", 100, {"Tackle"});
  state.teams[1][0] = buildCombatant(data, "machamp", 100, {"CloseCombat"});
  state.team_size = {1, 1};
  state.hazards[0] = SideHazards{1, 3, 2};
  REQUIRE_NOTHROW(validateState(state, data));

  SECTION("stealth rock above cap") {
    state.hazards[1].stealth_rock = 2;
    REQUIRE_THROWS_AS(validateState(state, data), std::invalid_argument);
  }
  SECTION("spikes above cap") {
    state.hazards[1].spikes = 4;
    REQUIRE_THROWS_AS(validateState(state, data), std::invalid_argument);
  }
  SECTION("negative toxic spikes") {
    state.hazards[1].toxic_spikes = -1;
    REQUIRE_THROWS_AS(validateState(state, data), std::invalid_argument);
  }
}
