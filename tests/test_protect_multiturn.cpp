#include "helpers.hpp"

#include "engine/core/battle_state.hpp"
#include "engine/core/data_loader.hpp"
#include "engine/core/engine.hpp"
#include "engine/core/rng.hpp"
#include "engine/model/status.hpp"

#include <catch2/catch_test_macros.hpp>

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

} // namespace

TEST_CASE("Whirlwind drags a benched opponent in, at -6 priority", "[phase9][forceswitch]") {
  DataLoader data;
  data.loadAll(BATTLE_ENGINE_DATA_DIR);
  BattleEngine engine(data);

  BattleState state;
  state.teams[0][0] = buildCombatant(data, "gyarados", 100, {"Whirlwind"});
  state.teams[1][0] = buildCombatant(data, "snorlax", 100, {"Tackle"});
  state.teams[1][1] = buildCombatant(data, "machamp", 100, {"CloseCombat"});
  state.team_size = {1, 2};

  FixedRNG rng(0.5f);
  auto events = engine.resolveTurn(state, UseMove{0}, UseMove{0}, rng);

  // -6 priority: the much slower Snorlax still moves first.
  REQUIRE(std::holds_alternative<MoveUsedEvent>(events.front()));
  REQUIRE(std::get<MoveUsedEvent>(events.front()).user.side == 1);
  // Then the drag happens.
  REQUIRE(countEvents<SwitchedInEvent>(events) == 1);
  REQUIRE(state.activeIndex[1] == 1);
}

TEST_CASE("Whirlwind fails on an empty bench", "[phase9][forceswitch]") {
  DataLoader data;
  data.loadAll(BATTLE_ENGINE_DATA_DIR);
  BattleEngine engine(data);

  BattleState state;
  state.teams[0][0] = buildCombatant(data, "gyarados", 100, {"Whirlwind"});
  state.teams[1][0] = buildCombatant(data, "snorlax", 100, {"Growl"});
  state.team_size = {1, 1};

  FixedRNG rng(0.5f);
  auto events = engine.resolveTurn(state, UseMove{0}, UseMove{0}, rng);
  REQUIRE(countEvents<MoveFailedEvent>(events) == 1);
}

TEST_CASE("Dragon Tail damages, then drags; damage-only on an empty bench",
          "[phase9][forceswitch]") {
  DataLoader data;
  data.loadAll(BATTLE_ENGINE_DATA_DIR);
  BattleEngine engine(data);

  BattleState state;
  state.teams[0][0] = buildCombatant(data, "gyarados", 100, {"DragonTail"});
  state.teams[1][0] = buildCombatant(data, "snorlax", 100, {"Growl"});
  state.teams[1][1] = buildCombatant(data, "machamp", 100, {"CloseCombat"});
  state.team_size = {1, 2};

  FixedRNG rng(0.5f);
  auto events = engine.resolveTurn(state, UseMove{0}, UseMove{0}, rng);
  REQUIRE(damageOn(events, 1) > 0);
  REQUIRE(state.activeIndex[1] == 1);

  // Empty bench: the damage stands, no failure, no switch.
  BattleState solo;
  solo.teams[0][0] = buildCombatant(data, "gyarados", 100, {"DragonTail"});
  solo.teams[1][0] = buildCombatant(data, "snorlax", 100, {"Growl"});
  solo.team_size = {1, 1};
  auto e2 = engine.resolveTurn(solo, UseMove{0}, UseMove{0}, rng);
  REQUIRE(damageOn(e2, 1) > 0);
  REQUIRE(countEvents<MoveFailedEvent>(e2) == 0);
  REQUIRE(countEvents<SwitchedInEvent>(e2) == 0);
}

TEST_CASE("A dragged-in Pokemon takes hazards and fires its ability", "[phase9][forceswitch]") {
  DataLoader data;
  data.loadAll(BATTLE_ENGINE_DATA_DIR);
  BattleEngine engine(data);

  BattleState state;
  state.teams[0][0] = buildCombatant(data, "snorlax", 100, {"Whirlwind"});
  state.teams[1][0] = buildCombatant(data, "machamp", 100, {"Growl"});
  state.teams[1][1] = buildCombatant(data, "gyarados", 100, {"Surf"});
  state.team_size = {1, 2};
  state.hazards[1].stealth_rock = 1;

  FixedRNG rng(0.5f);
  auto events = engine.resolveTurn(state, UseMove{0}, UseMove{0}, rng);

  REQUIRE(countEvents<HazardDamageEvent>(events) == 1); // Gyarados eats the rocks (4x)
  bool intimidate = false;
  for (const auto &ev : events)
    if (auto *e = std::get_if<AbilityTriggeredEvent>(&ev))
      if (e->ability == "Intimidate")
        intimidate = true;
  REQUIRE(intimidate);
}

TEST_CASE("Protect blocks a damaging move", "[phase9][protect]") {
  DataLoader data;
  data.loadAll(BATTLE_ENGINE_DATA_DIR);
  BattleEngine engine(data);

  BattleState state;
  state.teams[0][0] = buildCombatant(data, "gengar", 100, {"Protect"});
  state.teams[1][0] = buildCombatant(data, "machamp", 100, {"StoneEdge"});
  state.team_size = {1, 1};

  FixedRNG rng(0.5f);
  auto events = engine.resolveTurn(state, UseMove{0}, UseMove{0}, rng);

  REQUIRE(countEvents<ProtectedEvent>(events) == 2); // success + block
  REQUIRE(state.teams[0][0].currentHp == state.teams[0][0].stats.hp);
}

TEST_CASE("Chained Protects succeed at 1/3^n and the chain resets after a pause",
          "[phase9][protect]") {
  DataLoader data;
  data.loadAll(BATTLE_ENGINE_DATA_DIR);
  BattleEngine engine(data);

  BattleState state;
  state.teams[0][0] = buildCombatant(data, "gengar", 100, {"Protect", "ShadowBall"});
  state.teams[1][0] = buildCombatant(data, "machamp", 100, {"StoneEdge"});
  state.team_size = {1, 1};

  SECTION("second Protect fails when the 1/3 roll misses") {
    FixedRNG rng(0.5f); // chance(1/3): 0.5 > 0.333 -> fail
    engine.resolveTurn(state, UseMove{0}, UseMove{0}, rng);
    auto t2 = engine.resolveTurn(state, UseMove{0}, UseMove{0}, rng);
    REQUIRE(countEvents<MoveFailedEvent>(t2) == 1);
    REQUIRE(state.teams[0][0].currentHp < state.teams[0][0].stats.hp); // StoneEdge landed
  }

  SECTION("second Protect succeeds when the roll hits") {
    FixedRNG rng(0.2f); // 0.2 < 1/3 -> chained success (and 0.2 > 1/8: no crit)
    engine.resolveTurn(state, UseMove{0}, UseMove{0}, rng);
    auto t2 = engine.resolveTurn(state, UseMove{0}, UseMove{0}, rng);
    REQUIRE(countEvents<ProtectedEvent>(t2) == 2);
    REQUIRE(state.teams[0][0].currentHp == state.teams[0][0].stats.hp);
  }

  SECTION("a turn without Protect resets the chain") {
    FixedRNG rng(0.99f);                                              // any chained roll would fail
    engine.resolveTurn(state, UseMove{0}, UseMove{0}, rng);           // Protect (chain 1)
    engine.resolveTurn(state, UseMove{1}, UseMove{0}, rng);           // ShadowBall (chain resets)
    auto t3 = engine.resolveTurn(state, UseMove{0}, UseMove{0}, rng); // fresh Protect
    REQUIRE(countEvents<ProtectedEvent>(t3) == 2);
  }
}

TEST_CASE("Protect lets field moves through and Whirlwind bypasses it", "[phase9][protect]") {
  DataLoader data;
  data.loadAll(BATTLE_ENGINE_DATA_DIR);
  BattleEngine engine(data);

  BattleState state;
  state.teams[0][0] = buildCombatant(data, "gengar", 100, {"Protect"});
  state.teams[1][0] = buildCombatant(data, "tyranitar", 100, {"StealthRock", "StoneEdge"});
  state.team_size = {1, 1};

  FixedRNG rng(0.5f);
  auto t1 = engine.resolveTurn(state, UseMove{0}, UseMove{0}, rng);
  REQUIRE(countEvents<HazardSetEvent>(t1) == 1); // Stealth Rock ignores Protect

  BattleState state2;
  state2.teams[0][0] = buildCombatant(data, "gengar", 100, {"Protect"});
  state2.teams[1][0] = buildCombatant(data, "snorlax", 100, {"Whirlwind"});
  state2.teams[0][1] = buildCombatant(data, "machamp", 100, {"CloseCombat"});
  state2.team_size = {2, 1};
  auto t2 = engine.resolveTurn(state2, UseMove{0}, UseMove{0}, rng);
  (void)t2;
  REQUIRE(state2.activeIndex[0] == 1); // dragged out despite Protect
}

TEST_CASE("Two-turn Fly: charge, semi-invulnerability, forced release", "[phase9][twoturn]") {
  DataLoader data;
  data.loadAll(BATTLE_ENGINE_DATA_DIR);
  BattleEngine engine(data);

  BattleState state;
  state.teams[0][0] = buildCombatant(data, "charizard", 100, {"Fly", "Flamethrower"});
  state.teams[0][1] = buildCombatant(data, "gyarados", 100, {"Surf"});
  state.teams[1][0] = buildCombatant(data, "snorlax", 100, {"Tackle"});
  state.team_size = {2, 1};

  FixedRNG rng(0.99f);
  auto t1 = engine.resolveTurn(state, UseMove{0}, UseMove{0}, rng);

  REQUIRE(countEvents<ChargingEvent>(t1) == 1);
  REQUIRE(countEvents<MissedEvent>(t1) == 1); // Tackle whiffs on the airborne target
  REQUIRE(state.teams[0][0].charging_move_id != kNoMove);
  REQUIRE(state.teams[0][0].invulnerable_state == 1);

  // Turn 2: the provided action (even a switch) is ignored, Fly releases.
  auto t2 = engine.resolveTurn(state, SwitchAction{1}, UseMove{0}, rng);
  REQUIRE(state.activeIndex[0] == 0); // no switch happened
  REQUIRE(damageOn(t2, 1) > 0);       // Fly connected
  REQUIRE(state.teams[0][0].charging_move_id == kNoMove);
  REQUIRE(state.teams[0][0].invulnerable_state == 0);
}

TEST_CASE("Earthquake reaches a digging target and hits twice as hard", "[phase9][twoturn]") {
  DataLoader data;
  data.loadAll(BATTLE_ENGINE_DATA_DIR);
  BattleEngine engine(data);

  auto eqDamage = [&](bool digging) {
    BattleState state;
    // SwordsDance (self) as the baseline: unlike Growl it doesn't weaken the
    // incoming Earthquake, and it doesn't change the damage Garchomp takes.
    state.teams[0][0] = buildCombatant(data, "garchomp", 100, {digging ? "Dig" : "SwordsDance"});
    state.teams[1][0] = buildCombatant(data, "machamp", 100, {"Earthquake"});
    state.team_size = {1, 1};
    FixedRNG rng(0.99f);
    auto events = engine.resolveTurn(state, UseMove{0}, UseMove{0}, rng);
    return damageOn(events, 0);
  };

  int normal = eqDamage(false);
  int dug = eqDamage(true);
  REQUIRE(normal > 0);
  REQUIRE(dug >= normal * 2 - 2);
  REQUIRE(dug <= normal * 2 + 2);
}

TEST_CASE("SolarBeam skips the charge in the sun and is halved in the rain", "[phase9][twoturn]") {
  DataLoader data;
  data.loadAll(BATTLE_ENGINE_DATA_DIR);
  BattleEngine engine(data);

  auto fire = [&](Weather w) {
    BattleState state;
    state.teams[0][0] = buildCombatant(data, "venusaur", 100, {"SolarBeam"});
    state.teams[1][0] = buildCombatant(data, "machamp", 100, {"Growl"});
    state.team_size = {1, 1};
    state.weather = w;
    state.weather_turns_left = (w == Weather::None) ? 0 : 5;
    FixedRNG rng(0.99f);
    auto t1 = engine.resolveTurn(state, UseMove{0}, UseMove{0}, rng);
    int d1 = damageOn(t1, 1);
    if (d1 >= 0)
      return std::make_pair(1, d1); // fired on turn 1
    auto t2 = engine.resolveTurn(state, UseMove{0}, UseMove{0}, rng);
    return std::make_pair(2, damageOn(t2, 1));
  };

  auto [sunTurns, sunDmg] = fire(Weather::Sun);
  auto [dryTurns, dryDmg] = fire(Weather::None);
  auto [rainTurns, rainDmg] = fire(Weather::Rain);

  REQUIRE(sunTurns == 1); // no charge under the sun
  REQUIRE(dryTurns == 2); // normal two-turn behavior
  REQUIRE(rainTurns == 2);
  REQUIRE(sunDmg == dryDmg); // sun only skips the charge; it doesn't boost Grass
  REQUIRE(rainDmg < dryDmg);
  REQUIRE(rainDmg >= dryDmg / 2 - 2);
  REQUIRE(rainDmg <= dryDmg / 2 + 2);
}

TEST_CASE("An interrupted charge is lost", "[phase9][twoturn]") {
  DataLoader data;
  data.loadAll(BATTLE_ENGINE_DATA_DIR);
  BattleEngine engine(data);

  BattleState state;
  state.teams[0][0] = buildCombatant(data, "charizard", 100, {"Fly"});
  state.teams[1][0] = buildCombatant(data, "snorlax", 100, {"Growl"});
  state.team_size = {1, 1};

  FixedRNG charge(0.99f);
  engine.resolveTurn(state, UseMove{0}, UseMove{0}, charge);
  REQUIRE(state.teams[0][0].charging_move_id != kNoMove);

  // Fully paralyzed on the release turn: the charge is wasted.
  state.teams[0][0].status = Status::Paralysis;
  FixedRNG para(0.1f); // 0.1 < 0.25: full paralysis
  auto t2 = engine.resolveTurn(state, UseMove{0}, UseMove{0}, para);

  REQUIRE(countEvents<MoveSkippedEvent>(t2) == 1);
  REQUIRE(countEvents<DamageDealtEvent>(t2) == 0);
  REQUIRE(state.teams[0][0].charging_move_id == kNoMove);
  REQUIRE(state.teams[0][0].invulnerable_state == 0);
}
