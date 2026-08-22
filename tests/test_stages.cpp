#include "helpers.hpp"

#include "engine/core/battle_state.hpp"
#include "engine/core/data_loader.hpp"
#include "engine/core/engine.hpp"
#include "engine/core/rng.hpp"
#include "engine/core/validate.hpp"
#include "engine/model/stats.hpp"

#include <catch2/catch_test_macros.hpp>

#include <variant>

using namespace engine;
using engine::test::buildCombatant;

namespace {

int moveDamageOn(const EventLog &events, int side) {
  for (const auto &ev : events)
    if (auto *e = std::get_if<DamageDealtEvent>(&ev))
      if (e->target.side == side)
        return e->damage;
  return -1;
}

template <typename E> bool hasEvent(const EventLog &events) {
  for (const auto &ev : events)
    if (std::holds_alternative<E>(ev))
      return true;
  return false;
}

} // namespace

TEST_CASE("stageMultiplier matches canon values", "[stats][stage]") {
  REQUIRE(stageMultiplier(0) == 1.0f);
  REQUIRE(stageMultiplier(1) == 1.5f);
  REQUIRE(stageMultiplier(2) == 2.0f);
  REQUIRE(stageMultiplier(6) == 4.0f);
  REQUIRE(stageMultiplier(-1) == 2.0f / 3.0f);
  REQUIRE(stageMultiplier(-2) == 0.5f);
  REQUIRE(stageMultiplier(-6) == 0.25f);
}

TEST_CASE("stageMultiplier clamps beyond +-6", "[stats][stage]") {
  REQUIRE(stageMultiplier(7) == stageMultiplier(6));
  REQUIRE(stageMultiplier(-7) == stageMultiplier(-6));
}

TEST_CASE("SwordsDance doubles physical damage next hit", "[stage][damage]") {
  DataLoader data;
  engine::test::loadAll(data);
  BattleEngine engine(data);

  auto hitDamage = [&](bool boosted) {
    BattleState state;
    state.teams[0][0] = buildCombatant(data, "Conkeldurr", {"CloseCombat"});
    state.teams[1][0] = buildCombatant(data, "Snorlax", {"Tackle"});
    state.team_size = {1, 1};
    if (boosted)
      state.teams[0][0].stat_stages[static_cast<size_t>(StatIndex::Atk)] = 2;
    FixedRNG rng(0.5f);
    auto events = engine.resolveTurn(state, UseMove{0}, UseMove{0}, rng);
    return moveDamageOn(events, 1);
  };

  int plain = hitDamage(false);
  int boosted = hitDamage(true);
  // +2 Atk = x2.0 on the attack stat. The formula's +2 constant and floors
  // keep the final ratio close to but not exactly 2x; allow a small band.
  REQUIRE(boosted > plain);
  REQUIRE(boosted >= 2 * plain - 6);
  REQUIRE(boosted <= 2 * plain + 6);
}

TEST_CASE("StatChange effect raises the user's stage via SwordsDance", "[stage][effect]") {
  DataLoader data;
  engine::test::loadAll(data);

  BattleState state;
  state.teams[0][0] = buildCombatant(data, "Conkeldurr", {"SwordsDance"});
  state.teams[1][0] = buildCombatant(data, "Snorlax", {"Tackle"});
  state.team_size = {1, 1};

  BattleEngine engine(data);
  FixedRNG rng(0.5f);
  auto events = engine.resolveTurn(state, UseMove{0}, UseMove{0}, rng);

  REQUIRE(hasEvent<StatStageChangedEvent>(events));
  REQUIRE(state.teams[0][0].stat_stages[static_cast<size_t>(StatIndex::Atk)] == 2);
}

TEST_CASE("Growl lowers the target's Attack stage", "[stage][effect]") {
  DataLoader data;
  engine::test::loadAll(data);

  BattleState state;
  state.teams[0][0] = buildCombatant(data, "MegaGengar", {"Growl"});
  state.teams[1][0] = buildCombatant(data, "Conkeldurr", {"Tackle"});
  state.team_size = {1, 1};

  BattleEngine engine(data);
  FixedRNG rng(0.5f);
  auto events = engine.resolveTurn(state, UseMove{0}, UseMove{0}, rng);

  REQUIRE(hasEvent<StatStageChangedEvent>(events));
  REQUIRE(state.teams[1][0].stat_stages[static_cast<size_t>(StatIndex::Atk)] == -1);
}

TEST_CASE("Stage raise fails at +6 cap", "[stage][effect]") {
  DataLoader data;
  engine::test::loadAll(data);

  BattleState state;
  state.teams[0][0] = buildCombatant(data, "Conkeldurr", {"SwordsDance"});
  state.teams[1][0] = buildCombatant(data, "Snorlax", {"Tackle"});
  state.team_size = {1, 1};
  state.teams[0][0].stat_stages[static_cast<size_t>(StatIndex::Atk)] = 6;

  BattleEngine engine(data);
  FixedRNG rng(0.5f);
  auto events = engine.resolveTurn(state, UseMove{0}, UseMove{0}, rng);

  REQUIRE(hasEvent<StatChangeFailedEvent>(events));
  REQUIRE(state.teams[0][0].stat_stages[static_cast<size_t>(StatIndex::Atk)] == 6);
}

TEST_CASE("Speed stage flips turn order", "[stage][order]") {
  DataLoader data;
  engine::test::loadAll(data);
  BattleEngine engine(data);

  // Snorlax (Spd 30 base) is far slower than Gengar (Spd 110 base).
  auto firstMover = [&](int snorlaxSpeStage) {
    BattleState state;
    state.teams[0][0] = buildCombatant(data, "Snorlax", {"BodySlam"});
    state.teams[1][0] = buildCombatant(data, "Conkeldurr", {"CloseCombat"});
    state.team_size = {1, 1};
    state.teams[0][0].stat_stages[static_cast<size_t>(StatIndex::Spe)] = snorlaxSpeStage;
    FixedRNG rng(0.5f);
    auto events = engine.resolveTurn(state, UseMove{0}, UseMove{0}, rng);
    auto *first = std::get_if<MoveUsedEvent>(&events.front());
    REQUIRE(first != nullptr);
    return first->user.side;
  };

  REQUIRE(firstMover(0) == 1); // Conkeldurr (126) first normally
  // +6 Spe = x4 on Snorlax's 96: 384 clears Conkeldurr.
  REQUIRE(firstMover(6) == 0);
}

TEST_CASE("validateState rejects out-of-range stat stage", "[stage][validate]") {
  DataLoader data;
  engine::test::loadAll(data);

  BattleState state;
  state.teams[0][0] = buildCombatant(data, "Conkeldurr", {"SwordsDance"});
  state.teams[1][0] = buildCombatant(data, "Snorlax", {"Tackle"});
  state.team_size = {1, 1};
  state.activeIndex = {0, 0};
  state.teams[0][0].stat_stages[static_cast<size_t>(StatIndex::Atk)] = 7;

  REQUIRE_THROWS_AS(validateState(state, data), std::invalid_argument);
}

TEST_CASE("validateState accepts stages at the caps", "[stage][validate]") {
  DataLoader data;
  engine::test::loadAll(data);

  BattleState state;
  state.teams[0][0] = buildCombatant(data, "Conkeldurr", {"SwordsDance"});
  state.teams[1][0] = buildCombatant(data, "Snorlax", {"Tackle"});
  state.team_size = {1, 1};
  state.activeIndex = {0, 0};
  state.teams[0][0].stat_stages[static_cast<size_t>(StatIndex::Atk)] = kMaxStage;
  state.teams[1][0].stat_stages[static_cast<size_t>(StatIndex::Def)] = kMinStage;

  REQUIRE_NOTHROW(validateState(state, data));
}
