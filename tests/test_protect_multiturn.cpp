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

TEST_CASE("Whirlwind drags a benched opponent in, at -6 priority", "[protect][forceswitch]") {
  DataLoader data;
  engine::test::loadAll(data);
  BattleEngine engine(data);

  BattleState state;
  state.teams[0][0] = buildCombatant(data, "Gyarados", {"Whirlwind"});
  state.teams[1][0] = buildCombatant(data, "Snorlax", {"Tackle"});
  state.teams[1][1] = buildCombatant(data, "Conkeldurr", {"CloseCombat"});
  state.team_size = {1, 2};

  FixedRNG rng(0.5f);
  auto events = engine.resolveTurn(state, UseMove{0}, UseMove{0}, rng);

  // -6 priority: the much slower Snorlax still moves first.
  REQUIRE(std::holds_alternative<MoveUsedEvent>(events.front()));
  REQUIRE(std::get<MoveUsedEvent>(events.front()).user.side == 1);
  REQUIRE(countEvents<SwitchedInEvent>(events) == 1);
  REQUIRE(state.activeIndex[1] == 1);
}

TEST_CASE("Whirlwind fails on an empty bench", "[protect][forceswitch]") {
  DataLoader data;
  engine::test::loadAll(data);
  BattleEngine engine(data);

  BattleState state;
  state.teams[0][0] = buildCombatant(data, "Gyarados", {"Whirlwind"});
  state.teams[1][0] = buildCombatant(data, "Snorlax", {"Growl"});
  state.team_size = {1, 1};

  FixedRNG rng(0.5f);
  auto events = engine.resolveTurn(state, UseMove{0}, UseMove{0}, rng);
  REQUIRE(countEvents<MoveFailedEvent>(events) == 1);
}

TEST_CASE("Dragon Tail damages, then drags; damage-only on an empty bench",
          "[protect][forceswitch]") {
  DataLoader data;
  engine::test::loadAll(data);
  BattleEngine engine(data);

  BattleState state;
  state.teams[0][0] = buildCombatant(data, "Gyarados", {"DragonTail"});
  state.teams[1][0] = buildCombatant(data, "Snorlax", {"Growl"});
  state.teams[1][1] = buildCombatant(data, "Conkeldurr", {"CloseCombat"});
  state.team_size = {1, 2};

  FixedRNG rng(0.5f);
  auto events = engine.resolveTurn(state, UseMove{0}, UseMove{0}, rng);
  REQUIRE(damageOn(events, 1) > 0);
  REQUIRE(state.activeIndex[1] == 1);

  BattleState solo;
  solo.teams[0][0] = buildCombatant(data, "Gyarados", {"DragonTail"});
  solo.teams[1][0] = buildCombatant(data, "Snorlax", {"Growl"});
  solo.team_size = {1, 1};
  auto e2 = engine.resolveTurn(solo, UseMove{0}, UseMove{0}, rng);
  REQUIRE(damageOn(e2, 1) > 0);
  REQUIRE(countEvents<MoveFailedEvent>(e2) == 0);
  REQUIRE(countEvents<SwitchedInEvent>(e2) == 0);
}

TEST_CASE("A dragged-in Pokemon takes hazards and fires its ability", "[protect][forceswitch]") {
  DataLoader data;
  engine::test::loadAll(data);
  engine::test::overrideAbility(data, "Gyarados", "Intimidate");
  BattleEngine engine(data);

  BattleState state;
  state.teams[0][0] = buildCombatant(data, "Snorlax", {"Whirlwind"});
  state.teams[1][0] = buildCombatant(data, "Conkeldurr", {"Growl"});
  state.teams[1][1] = buildCombatant(data, "Gyarados", {"Surf"});
  state.team_size = {1, 2};
  state.hazards[1].stealth_rock = 1;

  FixedRNG rng(0.5f);
  auto events = engine.resolveTurn(state, UseMove{0}, UseMove{0}, rng);

  REQUIRE(countEvents<HazardDamageEvent>(events) == 1);
  bool intimidate = false;
  for (const auto &ev : events)
    if (auto *e = std::get_if<AbilityTriggeredEvent>(&ev))
      if (e->ability == "Intimidate")
        intimidate = true;
  REQUIRE(intimidate);
}

TEST_CASE("Protect blocks a damaging move", "[protect]") {
  DataLoader data;
  engine::test::loadAll(data);
  BattleEngine engine(data);

  BattleState state;
  state.teams[0][0] = buildCombatant(data, "MegaGengar", {"Protect"});
  state.teams[1][0] = buildCombatant(data, "Conkeldurr", {"StoneEdge"});
  state.team_size = {1, 1};

  FixedRNG rng(0.5f);
  auto events = engine.resolveTurn(state, UseMove{0}, UseMove{0}, rng);

  REQUIRE(countEvents<ProtectedEvent>(events) == 2);
  REQUIRE(state.teams[0][0].currentHp == state.teams[0][0].stats.hp);
}

TEST_CASE("Chained Protects succeed at 1/3^n and the chain resets after a pause",
          "[protect]") {
  DataLoader data;
  engine::test::loadAll(data);
  BattleEngine engine(data);

  BattleState state;
  state.teams[0][0] = buildCombatant(data, "MegaGengar", {"Protect", "ShadowBall"});
  state.teams[1][0] = buildCombatant(data, "Conkeldurr", {"StoneEdge"});
  state.team_size = {1, 1};

  SECTION("second Protect fails when the 1/3 roll misses") {
    FixedRNG rng(0.5f); // chance(1/3): 0.5 > 0.333 -> fail
    engine.resolveTurn(state, UseMove{0}, UseMove{0}, rng);
    auto t2 = engine.resolveTurn(state, UseMove{0}, UseMove{0}, rng);
    REQUIRE(countEvents<MoveFailedEvent>(t2) == 1);
    REQUIRE(state.teams[0][0].currentHp < state.teams[0][0].stats.hp);
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
    engine.resolveTurn(state, UseMove{0}, UseMove{0}, rng);
    engine.resolveTurn(state, UseMove{1}, UseMove{0}, rng);
    auto t3 = engine.resolveTurn(state, UseMove{0}, UseMove{0}, rng);
    REQUIRE(countEvents<ProtectedEvent>(t3) == 2);
  }
}

TEST_CASE("Protect lets field moves through and Whirlwind bypasses it", "[protect]") {
  DataLoader data;
  engine::test::loadAll(data);
  BattleEngine engine(data);

  BattleState state;
  state.teams[0][0] = buildCombatant(data, "MegaGengar", {"Protect"});
  state.teams[1][0] = buildCombatant(data, "Aerodactyl", {"StealthRock", "StoneEdge"});
  state.team_size = {1, 1};

  FixedRNG rng(0.5f);
  auto t1 = engine.resolveTurn(state, UseMove{0}, UseMove{0}, rng);
  REQUIRE(countEvents<HazardSetEvent>(t1) == 1);

  BattleState state2;
  state2.teams[0][0] = buildCombatant(data, "MegaGengar", {"Protect"});
  state2.teams[1][0] = buildCombatant(data, "Snorlax", {"Whirlwind"});
  state2.teams[0][1] = buildCombatant(data, "Conkeldurr", {"CloseCombat"});
  state2.team_size = {2, 1};
  auto t2 = engine.resolveTurn(state2, UseMove{0}, UseMove{0}, rng);
  (void)t2;
  REQUIRE(state2.activeIndex[0] == 1);
}

TEST_CASE("Two-turn Fly: charge, semi-invulnerability, forced release", "[protect][twoturn]") {
  DataLoader data;
  engine::test::loadAll(data);
  BattleEngine engine(data);

  BattleState state;
  state.teams[0][0] = buildCombatant(data, "Infernape", {"Fly", "Flamethrower"});
  state.teams[0][1] = buildCombatant(data, "Gyarados", {"Surf"});
  state.teams[1][0] = buildCombatant(data, "Snorlax", {"Tackle"});
  state.team_size = {2, 1};

  FixedRNG rng(0.99f);
  auto t1 = engine.resolveTurn(state, UseMove{0}, UseMove{0}, rng);

  REQUIRE(countEvents<ChargingEvent>(t1) == 1);
  REQUIRE(countEvents<MissedEvent>(t1) == 1);
  REQUIRE(state.teams[0][0].charging_move_id != kNoMove);
  REQUIRE(state.teams[0][0].invulnerable_state == 1);

  auto t2 = engine.resolveTurn(state, SwitchAction{1}, UseMove{0}, rng);
  REQUIRE(state.activeIndex[0] == 0);
  REQUIRE(damageOn(t2, 1) > 0);
  REQUIRE(state.teams[0][0].charging_move_id == kNoMove);
  REQUIRE(state.teams[0][0].invulnerable_state == 0);
}

TEST_CASE("Earthquake reaches a digging target and hits twice as hard", "[protect][twoturn]") {
  DataLoader data;
  engine::test::loadAll(data);
  BattleEngine engine(data);

  auto eqDamage = [&](bool digging) {
    BattleState state;
    // SwordsDance (self) as the baseline: unlike Growl it doesn't weaken the
    // incoming Earthquake, and it doesn't change the damage Garchomp takes.
    state.teams[0][0] = buildCombatant(data, "Excadrill", {digging ? "Dig" : "SwordsDance"});
    state.teams[1][0] = buildCombatant(data, "Conkeldurr", {"Earthquake"});
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

TEST_CASE("SolarBeam skips the charge in the sun and is halved in the rain", "[protect][twoturn]") {
  DataLoader data;
  engine::test::loadAll(data);
  BattleEngine engine(data);

  auto fire = [&](Weather w) {
    BattleState state;
    state.teams[0][0] = buildCombatant(data, "Toxapex", {"SolarBeam"});
    state.teams[1][0] = buildCombatant(data, "Conkeldurr", {"Growl"});
    state.team_size = {1, 1};
    state.weather = w;
    state.weather_turns_left = (w == Weather::None) ? 0 : 5;
    FixedRNG rng(0.99f);
    auto t1 = engine.resolveTurn(state, UseMove{0}, UseMove{0}, rng);
    int d1 = damageOn(t1, 1);
    if (d1 >= 0)
      return std::make_pair(1, d1);
    auto t2 = engine.resolveTurn(state, UseMove{0}, UseMove{0}, rng);
    return std::make_pair(2, damageOn(t2, 1));
  };

  auto [sunTurns, sunDmg] = fire(Weather::Sun);
  auto [dryTurns, dryDmg] = fire(Weather::None);
  auto [rainTurns, rainDmg] = fire(Weather::Rain);

  REQUIRE(sunTurns == 1);
  REQUIRE(dryTurns == 2);
  REQUIRE(rainTurns == 2);
  REQUIRE(sunDmg == dryDmg);
  REQUIRE(rainDmg < dryDmg);
  REQUIRE(rainDmg >= dryDmg / 2 - 2);
  REQUIRE(rainDmg <= dryDmg / 2 + 2);
}

TEST_CASE("An interrupted charge is lost", "[protect][twoturn]") {
  DataLoader data;
  engine::test::loadAll(data);
  BattleEngine engine(data);

  BattleState state;
  state.teams[0][0] = buildCombatant(data, "Infernape", {"Fly"});
  state.teams[1][0] = buildCombatant(data, "Snorlax", {"Growl"});
  state.team_size = {1, 1};

  FixedRNG charge(0.99f);
  engine.resolveTurn(state, UseMove{0}, UseMove{0}, charge);
  REQUIRE(state.teams[0][0].charging_move_id != kNoMove);

  state.teams[0][0].status = Status::Paralysis;
  FixedRNG para(0.1f); // 0.1 < 0.25: full paralysis
  auto t2 = engine.resolveTurn(state, UseMove{0}, UseMove{0}, para);

  REQUIRE(countEvents<MoveSkippedEvent>(t2) == 1);
  REQUIRE(countEvents<DamageDealtEvent>(t2) == 0);
  REQUIRE(state.teams[0][0].charging_move_id == kNoMove);
  REQUIRE(state.teams[0][0].invulnerable_state == 0);
}
