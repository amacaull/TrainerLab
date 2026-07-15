#include "test_helpers.hpp"

#include "engine/battle_state.hpp"
#include "engine/data_loader.hpp"
#include "engine/engine.hpp"
#include "engine/rng.hpp"
#include "engine/status.hpp"
#include "engine/validate.hpp"

#include <catch2/catch_test_macros.hpp>

#include <variant>

using namespace engine;
using engine::test::buildCombatant;

namespace {

// FixedRNG(0.5f): never procs full para (0.25) nor thaw (0.20), always passes
// accuracy (chancePct), min damage roll, Sleep lasts exactly 1 turn.
// FixedRNG(0.1f): forces full para and thaw.

template <typename E> bool hasEvent(const EventLog &events) {
  for (const auto &ev : events)
    if (std::holds_alternative<E>(ev))
      return true;
  return false;
}

bool hasStatusApplied(const EventLog &events, int side, Status s) {
  for (const auto &ev : events)
    if (auto *e = std::get_if<StatusAppliedEvent>(&ev))
      if (e->target.side == side && e->status == s)
        return true;
  return false;
}

bool hasStatusFailed(const EventLog &events, int side, Status s) {
  for (const auto &ev : events)
    if (auto *e = std::get_if<StatusFailedEvent>(&ev))
      if (e->target.side == side && e->status == s)
        return true;
  return false;
}

bool hasSkip(const EventLog &events, int side, SkipReason r) {
  for (const auto &ev : events)
    if (auto *e = std::get_if<MoveSkippedEvent>(&ev))
      if (e->user.side == side && e->reason == r)
        return true;
  return false;
}

bool hasCured(const EventLog &events, int side, Status s) {
  for (const auto &ev : events)
    if (auto *e = std::get_if<StatusCuredEvent>(&ev))
      if (e->who.side == side && e->status == s)
        return true;
  return false;
}

int statusDamageOn(const EventLog &events, int side) {
  for (const auto &ev : events)
    if (auto *e = std::get_if<StatusDamageEvent>(&ev))
      if (e->target.side == side)
        return e->damage;
  return -1;
}

int moveDamageOn(const EventLog &events, int side) {
  for (const auto &ev : events)
    if (auto *e = std::get_if<DamageDealtEvent>(&ev))
      if (e->target.side == side)
        return e->damage;
  return -1;
}

} // namespace

TEST_CASE("WillOWisp applies Burn", "[status]") {
  DataLoader data;
  data.loadAll(BATTLE_ENGINE_DATA_DIR);

  BattleState state;
  state.teams[0][0] = buildCombatant(data, "gengar", 50, {"WillOWisp"});
  state.teams[1][0] = buildCombatant(data, "snorlax", 50, {"Tackle"});
  state.team_size = {1, 1};

  BattleEngine engine(data);
  FixedRNG rng(0.5f);
  auto events = engine.resolveTurn(state, UseMove{0}, UseMove{0}, rng);

  REQUIRE(hasStatusApplied(events, 1, Status::Burn));
  REQUIRE(state.teams[1][0].status == Status::Burn);
}

TEST_CASE("Burn deals 1/16 max HP at end of turn", "[status][residual]") {
  DataLoader data;
  data.loadAll(BATTLE_ENGINE_DATA_DIR);

  BattleState state;
  state.teams[0][0] = buildCombatant(data, "gengar", 50, {"WillOWisp"});
  state.teams[1][0] = buildCombatant(data, "snorlax", 50, {"Tackle"});
  state.team_size = {1, 1};

  BattleEngine engine(data);
  FixedRNG rng(0.5f);
  auto events = engine.resolveTurn(state, UseMove{0}, UseMove{0}, rng);

  int maxHp = state.teams[1][0].stats.hp;
  REQUIRE(statusDamageOn(events, 1) == maxHp / 16);
  // Snorlax's Tackle hits Gengar for 0 (Normal vs Ghost), so its only HP
  // loss this turn is the burn chip.
  REQUIRE(state.teams[1][0].currentHp == maxHp - maxHp / 16);
}

TEST_CASE("Burn halves physical damage, leaves special damage untouched", "[status][damage]") {
  DataLoader data;
  data.loadAll(BATTLE_ENGINE_DATA_DIR);
  BattleEngine engine(data);

  auto runTurn = [&](const char *species, const char *move, Status attackerStatus) {
    BattleState state;
    state.teams[0][0] = buildCombatant(data, species, 50, {move});
    state.teams[1][0] = buildCombatant(data, "snorlax", 50, {"Tackle"});
    state.team_size = {1, 1};
    state.teams[0][0].status = attackerStatus;
    FixedRNG rng(0.5f);
    auto events = engine.resolveTurn(state, UseMove{0}, UseMove{0}, rng);
    return moveDamageOn(events, 1);
  };

  // Garchomp, not Machamp: Machamp has Guts, which inverts the burn penalty
  // into a x1.5 boost (covered in test_phase8). Earthquake is unaffected by
  // Snorlax's Thick Fat (Fire/Ice only) and by Garchomp's own Rough Skin.
  int physNormal = runTurn("garchomp", "Earthquake", Status::None);
  int physBurned = runTurn("garchomp", "Earthquake", Status::Burn);
  REQUIRE(physBurned < physNormal);
  REQUIRE(physBurned >= physNormal / 2 - 1);
  REQUIRE(physBurned <= physNormal / 2 + 1);

  int specNormal = runTurn("gengar", "ShadowBall", Status::None);
  int specBurned = runTurn("gengar", "ShadowBall", Status::Burn);
  REQUIRE(specBurned == specNormal);
}

TEST_CASE("Poison deals 1/8 max HP at end of turn", "[status][residual]") {
  DataLoader data;
  data.loadAll(BATTLE_ENGINE_DATA_DIR);

  BattleState state;
  state.teams[0][0] = buildCombatant(data, "gengar", 50, {"PoisonPowder"});
  state.teams[1][0] = buildCombatant(data, "snorlax", 50, {"Tackle"});
  state.team_size = {1, 1};

  BattleEngine engine(data);
  FixedRNG rng(0.5f);
  auto events = engine.resolveTurn(state, UseMove{0}, UseMove{0}, rng);

  REQUIRE(state.teams[1][0].status == Status::Poison);
  REQUIRE(statusDamageOn(events, 1) == state.teams[1][0].stats.hp / 8);
}

TEST_CASE("Toxic damage ramps n/16 per turn", "[status][residual]") {
  DataLoader data;
  data.loadAll(BATTLE_ENGINE_DATA_DIR);

  BattleState state;
  state.teams[0][0] = buildCombatant(data, "gengar", 50, {"Toxic"});
  state.teams[1][0] = buildCombatant(data, "snorlax", 50, {"Tackle"});
  state.team_size = {1, 1};

  BattleEngine engine(data);
  FixedRNG rng(0.5f);
  int maxHp = state.teams[1][0].stats.hp;

  auto t1 = engine.resolveTurn(state, UseMove{0}, UseMove{0}, rng);
  REQUIRE(statusDamageOn(t1, 1) == maxHp * 1 / 16);

  auto t2 = engine.resolveTurn(state, UseMove{0}, UseMove{0}, rng);
  REQUIRE(statusDamageOn(t2, 1) == maxHp * 2 / 16);
  // Re-applying Toxic on an already-statused target fails.
  REQUIRE(hasStatusFailed(t2, 1, Status::Toxic));

  auto t3 = engine.resolveTurn(state, UseMove{0}, UseMove{0}, rng);
  REQUIRE(statusDamageOn(t3, 1) == maxHp * 3 / 16);
}

TEST_CASE("Paralysis halves effective speed in turn order", "[status][order]") {
  DataLoader data;
  data.loadAll(BATTLE_ENGINE_DATA_DIR);
  BattleEngine engine(data);

  // Gengar (Spd 115) normally outspeeds Charizard (Spd 105).
  auto firstMover = [&](Status gengarStatus) {
    BattleState state;
    state.teams[0][0] = buildCombatant(data, "charizard", 50, {"DragonClaw"});
    state.teams[1][0] = buildCombatant(data, "gengar", 50, {"ShadowBall"});
    state.team_size = {1, 1};
    state.teams[1][0].status = gengarStatus;
    FixedRNG rng(0.5f);
    auto events = engine.resolveTurn(state, UseMove{0}, UseMove{0}, rng);
    auto *first = std::get_if<MoveUsedEvent>(&events.front());
    REQUIRE(first != nullptr);
    return first->user.side;
  };

  REQUIRE(firstMover(Status::None) == 1);
  REQUIRE(firstMover(Status::Paralysis) == 0); // 115/2 = 57 < 105
}

TEST_CASE("Full paralysis skips the move", "[status][before_move]") {
  DataLoader data;
  data.loadAll(BATTLE_ENGINE_DATA_DIR);

  BattleState state;
  state.teams[0][0] = buildCombatant(data, "pikachu", 50, {"QuickAttack"});
  state.teams[1][0] = buildCombatant(data, "gengar", 50, {"ShadowBall"});
  state.team_size = {1, 1};
  state.teams[1][0].status = Status::Paralysis;

  BattleEngine engine(data);
  FixedRNG rng(0.1f); // 0.1 < 0.25: full para procs
  int pikachuHp = state.teams[0][0].currentHp;
  auto events = engine.resolveTurn(state, UseMove{0}, UseMove{0}, rng);

  REQUIRE(hasSkip(events, 1, SkipReason::FullyParalyzed));
  REQUIRE(state.teams[0][0].currentHp == pikachuHp);
}

TEST_CASE("Sleep: target skips its turns then wakes up", "[status][before_move]") {
  DataLoader data;
  data.loadAll(BATTLE_ENGINE_DATA_DIR);

  BattleState state;
  state.teams[0][0] = buildCombatant(data, "venusaur", 50, {"Spore"});
  state.teams[1][0] = buildCombatant(data, "snorlax", 50, {"BodySlam"});
  state.team_size = {1, 1};

  BattleEngine engine(data);
  FixedRNG rng(0.5f); // rangeInt -> min: sleep lasts exactly 1 turn

  // Turn 1: Venusaur (faster) sleeps Snorlax, which then skips its move.
  auto t1 = engine.resolveTurn(state, UseMove{0}, UseMove{0}, rng);
  REQUIRE(hasStatusApplied(t1, 1, Status::Sleep));
  REQUIRE(hasSkip(t1, 1, SkipReason::Asleep));
  REQUIRE(state.teams[1][0].status == Status::Sleep);

  // Turn 2: re-Spore fails (already statused), Snorlax wakes up and moves.
  int venusaurHp = state.teams[0][0].currentHp;
  auto t2 = engine.resolveTurn(state, UseMove{0}, UseMove{0}, rng);
  REQUIRE(hasStatusFailed(t2, 1, Status::Sleep));
  REQUIRE(hasCured(t2, 1, Status::Sleep));
  REQUIRE(state.teams[1][0].status == Status::None);
  REQUIRE(state.teams[0][0].currentHp < venusaurHp);
}

TEST_CASE("Sleep Clause: applying Sleep fails if a teammate already sleeps", "[status][clause]") {
  DataLoader data;
  data.loadAll(BATTLE_ENGINE_DATA_DIR);

  BattleState state;
  state.teams[0][0] = buildCombatant(data, "venusaur", 50, {"Spore"});
  state.teams[1][0] = buildCombatant(data, "snorlax", 50, {"BodySlam"});
  state.teams[1][1] = buildCombatant(data, "pikachu", 50, {"Thunderbolt"});
  state.team_size = {1, 2};
  state.teams[1][1].status = Status::Sleep;
  state.teams[1][1].status_turns = 2;

  BattleEngine engine(data);
  FixedRNG rng(0.5f);
  auto events = engine.resolveTurn(state, UseMove{0}, UseMove{0}, rng);

  REQUIRE(hasStatusFailed(events, 1, Status::Sleep));
  REQUIRE(state.teams[1][0].status == Status::None);
}

TEST_CASE("Sleep Clause ignores fainted sleepers", "[status][clause]") {
  DataLoader data;
  data.loadAll(BATTLE_ENGINE_DATA_DIR);

  BattleState state;
  state.teams[0][0] = buildCombatant(data, "venusaur", 50, {"Spore"});
  state.teams[1][0] = buildCombatant(data, "snorlax", 50, {"BodySlam"});
  state.teams[1][1] = buildCombatant(data, "pikachu", 50, {"Thunderbolt"});
  state.team_size = {1, 2};
  state.teams[1][1].status = Status::Sleep;
  state.teams[1][1].currentHp = 0;

  BattleEngine engine(data);
  FixedRNG rng(0.5f);
  auto events = engine.resolveTurn(state, UseMove{0}, UseMove{0}, rng);

  REQUIRE(hasStatusApplied(events, 1, Status::Sleep));
}

TEST_CASE("A second status cannot replace the first", "[status]") {
  DataLoader data;
  data.loadAll(BATTLE_ENGINE_DATA_DIR);

  BattleState state;
  state.teams[0][0] = buildCombatant(data, "gengar", 50, {"WillOWisp"});
  state.teams[1][0] = buildCombatant(data, "snorlax", 50, {"Tackle"});
  state.team_size = {1, 1};
  state.teams[1][0].status = Status::Paralysis;

  BattleEngine engine(data);
  FixedRNG rng(0.5f);
  auto events = engine.resolveTurn(state, UseMove{0}, UseMove{0}, rng);

  REQUIRE(hasStatusFailed(events, 1, Status::Burn));
  REQUIRE(state.teams[1][0].status == Status::Paralysis);
}

TEST_CASE("Type immunities to statuses", "[status][types]") {
  DataLoader data;
  data.loadAll(BATTLE_ENGINE_DATA_DIR);
  BattleEngine engine(data);

  auto tryStatus = [&](const char *attacker, const char *move, const char *target) {
    BattleState state;
    state.teams[0][0] = buildCombatant(data, attacker, 50, {move});
    state.teams[1][0] = buildCombatant(data, target, 50, {"Tackle"});
    state.team_size = {1, 1};
    FixedRNG rng(0.5f);
    auto events = engine.resolveTurn(state, UseMove{0}, UseMove{0}, rng);
    return state.teams[1][0].status;
  };

  SECTION("Fire-types cannot be burned") {
    REQUIRE(tryStatus("gengar", "WillOWisp", "charizard") == Status::None);
  }
  SECTION("Electric-types cannot be paralyzed") {
    REQUIRE(tryStatus("gengar", "ThunderWave", "pikachu") == Status::None);
  }
  SECTION("Poison-types cannot be poisoned") {
    REQUIRE(tryStatus("snorlax", "Toxic", "gengar") == Status::None);
    REQUIRE(tryStatus("snorlax", "PoisonPowder", "venusaur") == Status::None);
  }
  SECTION("Type-chart immunity blocks status moves (ThunderWave vs Ground)") {
    REQUIRE(tryStatus("gengar", "ThunderWave", "garchomp") == Status::None);
  }
}

TEST_CASE("Frozen Pokemon skips its move, thaws on RNG proc", "[status][before_move]") {
  DataLoader data;
  data.loadAll(BATTLE_ENGINE_DATA_DIR);
  BattleEngine engine(data);

  auto runFrozenTurn = [&](float rngUnit) {
    BattleState state;
    state.teams[0][0] = buildCombatant(data, "blastoise", 50, {"Surf"});
    state.teams[1][0] = buildCombatant(data, "snorlax", 50, {"BodySlam"});
    state.team_size = {1, 1};
    state.teams[1][0].status = Status::Freeze;
    FixedRNG rng(rngUnit);
    return engine.resolveTurn(state, UseMove{0}, UseMove{0}, rng);
  };

  auto stayFrozen = runFrozenTurn(0.5f); // 0.5 >= 0.20: no thaw
  REQUIRE(hasSkip(stayFrozen, 1, SkipReason::Frozen));

  auto thawed = runFrozenTurn(0.1f); // 0.1 < 0.20: thaw, then move
  REQUIRE(hasCured(thawed, 1, Status::Freeze));
  REQUIRE_FALSE(hasSkip(thawed, 1, SkipReason::Frozen));
}

TEST_CASE("A damaging Fire move thaws a frozen target", "[status][damage]") {
  DataLoader data;
  data.loadAll(BATTLE_ENGINE_DATA_DIR);

  BattleState state;
  state.teams[0][0] = buildCombatant(data, "charizard", 50, {"Flamethrower"});
  state.teams[1][0] = buildCombatant(data, "blastoise", 50, {"Surf"});
  state.team_size = {1, 1};
  state.teams[1][0].status = Status::Freeze;

  BattleEngine engine(data);
  FixedRNG rng(0.5f); // no RNG thaw: only the Fire hit can cure
  auto events = engine.resolveTurn(state, UseMove{0}, UseMove{0}, rng);

  REQUIRE(hasCured(events, 1, Status::Freeze));
  REQUIRE(state.teams[1][0].status == Status::None);
}

TEST_CASE("Residual damage can faint and end the battle", "[status][residual]") {
  DataLoader data;
  data.loadAll(BATTLE_ENGINE_DATA_DIR);

  BattleState state;
  state.teams[0][0] = buildCombatant(data, "gengar", 50, {"WillOWisp"});
  state.teams[1][0] = buildCombatant(data, "snorlax", 50, {"Tackle"});
  state.team_size = {1, 1};
  state.teams[1][0].status = Status::Burn;
  state.teams[1][0].currentHp = 3; // below the 1/16 burn chip

  BattleEngine engine(data);
  FixedRNG rng(0.5f);
  auto events = engine.resolveTurn(state, UseMove{0}, UseMove{0}, rng);

  REQUIRE(hasEvent<FaintedEvent>(events));
  REQUIRE(state.sideHasLost(1));
}

TEST_CASE("validateState accepts a statused Pokemon", "[status][validate]") {
  DataLoader data;
  data.loadAll(BATTLE_ENGINE_DATA_DIR);

  BattleState state;
  state.teams[0][0] = buildCombatant(data, "gengar", 50, {"WillOWisp"});
  state.teams[1][0] = buildCombatant(data, "snorlax", 50, {"Tackle"});
  state.team_size = {1, 1};
  state.teams[1][0].status = Status::Toxic;
  state.teams[1][0].status_turns = 4;

  REQUIRE_NOTHROW(validateState(state, data));
}
