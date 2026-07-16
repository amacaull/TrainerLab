#include "helpers.hpp"

#include "engine/abilities/ability.hpp"
#include "engine/core/battle_state.hpp"
#include "engine/core/data_loader.hpp"
#include "engine/core/engine.hpp"
#include "engine/core/rng.hpp"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <variant>

using namespace engine;
using engine::test::buildCombatant;

namespace {

bool abilityTriggered(const EventLog &events, int side, const std::string &name) {
  for (const auto &ev : events)
    if (auto *e = std::get_if<AbilityTriggeredEvent>(&ev))
      if (e->who.side == side && e->ability == name)
        return true;
  return false;
}

int moveDamageOn(const EventLog &events, int side) {
  for (const auto &ev : events)
    if (auto *e = std::get_if<DamageDealtEvent>(&ev))
      if (e->target.side == side)
        return e->damage;
  return -1;
}

} // namespace

TEST_CASE("Ability registry: known names resolve, unknown returns nullptr", "[ability]") {
  REQUIRE(abilityByName("Blaze") != nullptr);
  REQUIRE(abilityByName("Torrent") != nullptr);
  REQUIRE(abilityByName("Overgrow") != nullptr);
  REQUIRE(abilityByName("Levitate") != nullptr);
  REQUIRE(abilityByName("Intimidate") != nullptr);
  REQUIRE(abilityByName("NoSuchAbility") == nullptr);
  REQUIRE(abilityByName("") == nullptr);
}

TEST_CASE("Every loaded species references a registered ability", "[ability][catalog]") {
  DataLoader data;
  data.loadAll(BATTLE_ENGINE_DATA_DIR);

  for (int sid = 0; sid < data.speciesCount(); ++sid) {
    const Species &sp = data.speciesByIndex(sid);
    INFO("species " << sp.id << " ability '" << sp.ability << "'");
    REQUIRE_FALSE(sp.ability.empty());
    REQUIRE(abilityByName(sp.ability) != nullptr);
  }
}

TEST_CASE("Intimidate lowers the opposing Attack on switch-in", "[ability][switch]") {
  DataLoader data;
  data.loadAll(BATTLE_ENGINE_DATA_DIR);
  BattleEngine engine(data);

  BattleState state;
  state.teams[0][0] = buildCombatant(data, "snorlax", 100, {"BodySlam"});
  state.teams[0][1] = buildCombatant(data, "gyarados", 100, {"UTurn"});
  state.teams[1][0] = buildCombatant(data, "machamp", 100, {"CloseCombat"});
  state.team_size = {2, 1};

  FixedRNG rng(0.5f);
  auto events = engine.resolveTurn(state, SwitchAction{1}, UseMove{0}, rng);

  REQUIRE(abilityTriggered(events, 0, "Intimidate"));
  REQUIRE(state.teams[1][0].stat_stages[static_cast<size_t>(StatIndex::Atk)] == -1);
}

TEST_CASE("Intimidate fires on a KO replacement", "[ability][replacement]") {
  DataLoader data;
  data.loadAll(BATTLE_ENGINE_DATA_DIR);
  BattleEngine engine(data);

  BattleState state;
  state.teams[0][0] = buildCombatant(data, "snorlax", 100, {"BodySlam"});
  state.teams[0][1] = buildCombatant(data, "gyarados", 100, {"UTurn"});
  state.teams[1][0] = buildCombatant(data, "machamp", 100, {"CloseCombat"});
  state.team_size = {2, 1};
  state.teams[0][0].currentHp = 0;

  auto events = engine.resolveReplacement(state, 0, 1);

  REQUIRE(abilityTriggered(events, 0, "Intimidate"));
  REQUIRE(state.teams[1][0].stat_stages[static_cast<size_t>(StatIndex::Atk)] == -1);
}

TEST_CASE("startBattle fires the leads' switch-in abilities", "[ability]") {
  DataLoader data;
  data.loadAll(BATTLE_ENGINE_DATA_DIR);
  BattleEngine engine(data);

  BattleState state;
  state.teams[0][0] = buildCombatant(data, "gyarados", 100, {"UTurn"});
  state.teams[1][0] = buildCombatant(data, "machamp", 100, {"CloseCombat"});
  state.team_size = {1, 1};

  FixedRNG srng(0.5f);
  auto events = engine.startBattle(state, srng);

  REQUIRE(abilityTriggered(events, 0, "Intimidate"));
  REQUIRE(state.teams[1][0].stat_stages[static_cast<size_t>(StatIndex::Atk)] == -1);
}

TEST_CASE("Intimidate at the -6 cap emits StatChangeFailed", "[ability][stage]") {
  DataLoader data;
  data.loadAll(BATTLE_ENGINE_DATA_DIR);
  BattleEngine engine(data);

  BattleState state;
  state.teams[0][0] = buildCombatant(data, "gyarados", 100, {"UTurn"});
  state.teams[1][0] = buildCombatant(data, "machamp", 100, {"CloseCombat"});
  state.team_size = {1, 1};
  state.teams[1][0].stat_stages[static_cast<size_t>(StatIndex::Atk)] = -6;

  FixedRNG srng(0.5f);
  auto events = engine.startBattle(state, srng);

  bool failed = false;
  for (const auto &ev : events)
    if (std::holds_alternative<StatChangeFailedEvent>(ev))
      failed = true;
  REQUIRE(failed);
  REQUIRE(state.teams[1][0].stat_stages[static_cast<size_t>(StatIndex::Atk)] == -6);
}

TEST_CASE("Levitate voids Ground moves entirely", "[ability][immunity]") {
  DataLoader data;
  data.loadAll(BATTLE_ENGINE_DATA_DIR);
  BattleEngine engine(data);

  BattleState state;
  state.teams[0][0] = buildCombatant(data, "garchomp", 100, {"Earthquake", "DragonClaw"});
  state.teams[1][0] = buildCombatant(data, "gengar", 100, {"ShadowBall"});
  state.team_size = {1, 1};

  int gengarHp = state.teams[1][0].currentHp;
  FixedRNG rng(0.5f);
  auto events = engine.resolveTurn(state, UseMove{0}, UseMove{0}, rng);

  REQUIRE(abilityTriggered(events, 1, "Levitate"));
  REQUIRE(state.teams[1][0].currentHp == gengarHp);
  REQUIRE(moveDamageOn(events, 1) == -1); // no damage event at all

  // Non-Ground moves are unaffected.
  auto t2 = engine.resolveTurn(state, UseMove{1}, UseMove{0}, rng);
  REQUIRE(moveDamageOn(t2, 1) > 0);
}

TEST_CASE("Pinch abilities boost same-type damage at 1/3 HP", "[ability][damage]") {
  DataLoader data;
  data.loadAll(BATTLE_ENGINE_DATA_DIR);
  BattleEngine engine(data);

  struct Case {
    const char *species;
    const char *move;
  };
  const Case cases[] = {
      {"charizard", "Flamethrower"}, // Blaze
      {"blastoise", "Surf"},         // Torrent
      {"venusaur", "VineWhip"},      // Overgrow
  };

  auto hitDamage = [&](const Case &c, bool pinched) {
    BattleState state;
    state.teams[0][0] = buildCombatant(data, c.species, 50, {c.move});
    state.teams[1][0] = buildCombatant(data, "snorlax", 100, {"Tackle"});
    state.team_size = {1, 1};
    if (pinched)
      state.teams[0][0].currentHp = state.teams[0][0].stats.hp / 3;
    FixedRNG rng(0.5f);
    auto events = engine.resolveTurn(state, UseMove{0}, UseMove{0}, rng);
    return moveDamageOn(events, 1);
  };

  for (const auto &c : cases) {
    INFO(c.species << " / " << c.move);
    int normal = hitDamage(c, false);
    int pinched = hitDamage(c, true);
    // x1.5 within floor tolerance.
    REQUIRE(pinched > normal);
    REQUIRE(pinched >= normal * 3 / 2 - 2);
    REQUIRE(pinched <= normal * 3 / 2 + 2);
  }
}

TEST_CASE("Pinch abilities are inert above 1/3 HP and on off-type moves", "[ability][damage]") {
  DataLoader data;
  data.loadAll(BATTLE_ENGINE_DATA_DIR);
  BattleEngine engine(data);

  auto hitDamage = [&](const char *move, int hpFraction) {
    BattleState state;
    state.teams[0][0] = buildCombatant(data, "charizard", 100, {move});
    state.teams[1][0] = buildCombatant(data, "snorlax", 100, {"Tackle"});
    state.team_size = {1, 1};
    if (hpFraction > 0)
      state.teams[0][0].currentHp = state.teams[0][0].stats.hp / hpFraction;
    FixedRNG rng(0.5f);
    auto events = engine.resolveTurn(state, UseMove{0}, UseMove{0}, rng);
    return moveDamageOn(events, 1);
  };

  // Above the pinch threshold: no boost.
  REQUIRE(hitDamage("Flamethrower", 2) == hitDamage("Flamethrower", 0));
  // At 1/3 HP but wrong type (AirSlash is Flying): no boost either.
  REQUIRE(hitDamage("AirSlash", 3) == hitDamage("AirSlash", 0));
}
