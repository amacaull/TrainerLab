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

BattleState makeDuel(const DataLoader &data, const char *s0, std::vector<std::string> m0,
                     const char *s1, std::vector<std::string> m1) {
  BattleState state;
  state.teams[0][0] = buildCombatant(data, s0, 50, m0);
  state.teams[1][0] = buildCombatant(data, s1, 50, m1);
  state.team_size = {1, 1};
  return state;
}

} // namespace

TEST_CASE("Secondary effects proc or not through RNG::chance (ADR #26)", "[phase8][secondary]") {
  DataLoader data;
  data.loadAll(BATTLE_ENGINE_DATA_DIR);
  BattleEngine engine(data);

  // Flamethrower: 10% burn. FixedRNG(0.99) denies, FixedRNG(0.05) forces.
  auto burnAfter = [&](float unit) {
    auto state = makeDuel(data, "gengar", {"Flamethrower"}, "snorlax", {"Growl"});
    FixedRNG rng(unit);
    engine.resolveTurn(state, UseMove{0}, UseMove{0}, rng);
    return state.teams[1][0].status;
  };
  REQUIRE(burnAfter(0.99f) == Status::None);
  REQUIRE(burnAfter(0.05f) == Status::Burn);
}

TEST_CASE("Flinch skips the slower target's move and clears at end of turn", "[phase8][flinch]") {
  DataLoader data;
  data.loadAll(BATTLE_ENGINE_DATA_DIR);
  BattleEngine engine(data);

  // Charizard (100) AirSlash before Snorlax (30); forced 30% flinch.
  auto state = makeDuel(data, "charizard", {"AirSlash"}, "snorlax", {"Tackle"});
  FixedRNG rng(0.05f);
  auto events = engine.resolveTurn(state, UseMove{0}, UseMove{0}, rng);

  bool snorlaxFlinched = false;
  for (const auto &ev : events)
    if (auto *e = std::get_if<MoveSkippedEvent>(&ev))
      if (e->user.side == 1 && e->reason == SkipReason::Flinched)
        snorlaxFlinched = true;
  REQUIRE(snorlaxFlinched);
  REQUIRE(state.teams[0][0].currentHp == state.teams[0][0].stats.hp); // Tackle never came
  REQUIRE(state.teams[1][0].flinched == 0); // volatile cleared at end of turn
}

TEST_CASE("Crits multiply by 1.5 and flag the event", "[phase8][crit]") {
  DataLoader data;
  data.loadAll(BATTLE_ENGINE_DATA_DIR);
  BattleEngine engine(data);

  // Machamp uses Swords Dance (self): unlike Growl it doesn't lower Snorlax's
  // Attack, which a crit would then ignore and skew the ratio.
  auto hit = [&](float unit, bool *critOut) {
    auto state = makeDuel(data, "snorlax", {"Tackle"}, "machamp", {"SwordsDance"});
    FixedRNG rng(unit);
    auto events = engine.resolveTurn(state, UseMove{0}, UseMove{0}, rng);
    for (const auto &ev : events)
      if (auto *e = std::get_if<DamageDealtEvent>(&ev))
        if (e->target.side == 1) {
          if (critOut)
            *critOut = e->wasCrit;
          return e->damage;
        }
    return -1;
  };

  bool critLow = false, critHigh = false;
  int normal = hit(0.5f, &critLow); // 0.5 > 1/24: no crit
  int crit = hit(0.01f, &critHigh); // 0.01 < 1/24: crit
  REQUIRE_FALSE(critLow);
  REQUIRE(critHigh);
  REQUIRE(crit > normal);
  REQUIRE(crit >= normal * 3 / 2 - 2);
  REQUIRE(crit <= normal * 3 / 2 + 2);
}

TEST_CASE("A crit ignores the defender's defensive boosts", "[phase8][crit]") {
  DataLoader data;
  data.loadAll(BATTLE_ENGINE_DATA_DIR);
  BattleEngine engine(data);

  auto critHit = [&](int defStage) {
    auto state = makeDuel(data, "snorlax", {"Tackle"}, "machamp", {"Growl"});
    state.teams[1][0].stat_stages[static_cast<size_t>(StatIndex::Def)] = defStage;
    FixedRNG rng(0.01f);
    auto events = engine.resolveTurn(state, UseMove{0}, UseMove{0}, rng);
    return damageOn(events, 1);
  };

  REQUIRE(critHit(6) == critHit(0)); // +6 Def ignored on a crit
}

TEST_CASE("Recoil hits the user for a third of the damage dealt and can KO", "[phase8][recoil]") {
  DataLoader data;
  data.loadAll(BATTLE_ENGINE_DATA_DIR);
  BattleEngine engine(data);

  auto state = makeDuel(data, "pikachu", {"BraveBird"}, "snorlax", {"Growl"});
  FixedRNG rng(0.5f);
  auto events = engine.resolveTurn(state, UseMove{0}, UseMove{0}, rng);

  int dealt = damageOn(events, 1);
  REQUIRE(dealt > 0);
  int recoil = -1;
  for (const auto &ev : events)
    if (auto *e = std::get_if<RecoilDamageEvent>(&ev))
      recoil = e->damage;
  REQUIRE(recoil == std::max(1, dealt / 3));

  // Recoil can faint the user.
  auto state2 = makeDuel(data, "pikachu", {"BraveBird"}, "snorlax", {"Growl"});
  state2.teams[0][0].currentHp = 1;
  FixedRNG rng2(0.5f);
  auto events2 = engine.resolveTurn(state2, UseMove{0}, UseMove{0}, rng2);
  REQUIRE(state2.teams[0][0].isFainted());
  REQUIRE(countEvents<FaintedEvent>(events2) == 1);
}

TEST_CASE("Recover heals half the max HP and fails at full", "[phase8][recovery]") {
  DataLoader data;
  data.loadAll(BATTLE_ENGINE_DATA_DIR);
  BattleEngine engine(data);

  auto state = makeDuel(data, "politoed", {"Recover"}, "snorlax", {"Growl"});
  int maxHp = state.teams[0][0].stats.hp;
  state.teams[0][0].currentHp = maxHp / 4;

  FixedRNG rng(0.5f);
  auto t1 = engine.resolveTurn(state, UseMove{0}, UseMove{0}, rng);
  REQUIRE(countEvents<HealedEvent>(t1) == 1);
  REQUIRE(state.teams[0][0].currentHp == maxHp / 4 + maxHp / 2);

  state.teams[0][0].currentHp = maxHp;
  auto t2 = engine.resolveTurn(state, UseMove{0}, UseMove{0}, rng);
  REQUIRE(countEvents<MoveFailedEvent>(t2) == 1);
}

TEST_CASE("Roost heals and suppresses the Flying type until end of turn", "[phase8][recovery]") {
  DataLoader data;
  data.loadAll(BATTLE_ENGINE_DATA_DIR);
  BattleEngine engine(data);

  // Charizard (100) roosts before Pikachu (90) attacks: Electric hits a
  // pure Fire target for neutral instead of 2x.
  auto thunderboltOn = [&](const char *move0) {
    auto state = makeDuel(data, "charizard", {move0}, "pikachu", {"Thunderbolt"});
    state.teams[0][0].currentHp = state.teams[0][0].stats.hp / 2;
    FixedRNG rng(0.99f); // no crits, no paralysis secondary
    auto events = engine.resolveTurn(state, UseMove{0}, UseMove{0}, rng);
    for (const auto &ev : events)
      if (auto *e = std::get_if<DamageDealtEvent>(&ev))
        if (e->target.side == 0)
          return std::make_pair(e->damage, e->effectiveness);
    return std::make_pair(-1, 0.0f);
  };

  auto [dmgRoost, effRoost] = thunderboltOn("Roost");
  auto [dmgPlain, effPlain] = thunderboltOn("WillOWisp"); // filler action
  REQUIRE(effPlain == 2.0f);
  REQUIRE(effRoost == 1.0f);
  REQUIRE(dmgRoost < dmgPlain);
}

TEST_CASE("Rest fully heals, cures the old status and sleeps 2 turns outside the Sleep Clause",
          "[phase8][rest]") {
  DataLoader data;
  data.loadAll(BATTLE_ENGINE_DATA_DIR);
  BattleEngine engine(data);

  BattleState state;
  state.teams[0][0] = buildCombatant(data, "snorlax", 50, {"Rest", "Tackle"});
  state.teams[0][1] = buildCombatant(data, "machamp", 50, {"CloseCombat"});
  state.teams[1][0] = buildCombatant(data, "venusaur", 50, {"Spore"});
  state.team_size = {2, 1};

  state.teams[0][0].currentHp = 30;
  state.teams[0][0].status = Status::Burn;

  FixedRNG rng(0.99f);
  auto events = engine.resolveTurn(state, UseMove{0}, UseMove{0}, rng);
  (void)events;

  BattlePokemon &lax = state.teams[0][0];
  REQUIRE(lax.currentHp == lax.stats.hp);
  REQUIRE(lax.status == Status::Sleep);
  REQUIRE(lax.status_turns > 0);
  REQUIRE(lax.sleep_self_inflicted == 1);

  // Sleep Clause ignores the rested sleeper: Spore can still land on the
  // switched-in Machamp.
  auto t2 = engine.resolveTurn(state, SwitchAction{1}, UseMove{0}, rng);
  (void)t2;
  REQUIRE(state.teams[0][1].status == Status::Sleep);
  REQUIRE(state.teams[0][1].sleep_self_inflicted == 0);

  // Rest fails at full HP.
  auto state2 = makeDuel(data, "snorlax", {"Rest"}, "venusaur", {"Growl"});
  auto t3 = engine.resolveTurn(state2, UseMove{0}, UseMove{0}, rng);
  REQUIRE(countEvents<MoveFailedEvent>(t3) == 1);
}

TEST_CASE("Evasion stages make 100-accuracy moves missable (ADR #18 resolved)",
          "[phase8][accuracy]") {
  DataLoader data;
  data.loadAll(BATTLE_ENGINE_DATA_DIR);
  BattleEngine engine(data);

  auto missesIn100Turns = [&](int evaStage, const char *move) {
    auto state = makeDuel(data, "snorlax", {move}, "machamp", {"Growl"});
    state.teams[1][0].stat_stages[static_cast<size_t>(StatIndex::Evasion)] = evaStage;
    MersenneRNG rng(42);
    int misses = 0;
    for (int i = 0; i < 100; ++i) {
      state.teams[1][0].stat_stages[static_cast<size_t>(StatIndex::Evasion)] = evaStage;
      auto events = engine.resolveTurn(state, UseMove{0}, UseMove{0}, rng);
      misses += countEvents<MissedEvent>(events);
      state.teams[0][0].currentHp = state.teams[0][0].stats.hp; // keep it going
      state.teams[1][0].currentHp = state.teams[1][0].stats.hp;
      state.teams[0][0].status = Status::None;
      state.teams[1][0].status = Status::None;
    }
    return misses;
  };

  // +6 evasion: effective accuracy 100 * 3/9 = 33%. Expect ~67 misses.
  int evasive = missesIn100Turns(6, "Growl");
  REQUIRE(evasive > 40);
  REQUIRE(evasive < 90);
  // No stages: accuracy 100 never rolls, never misses.
  REQUIRE(missesIn100Turns(0, "Growl") == 0);
  // accuracy 0 moves (Swords Dance) bypass the roll entirely.
  REQUIRE(missesIn100Turns(6, "SwordsDance") == 0);
}

TEST_CASE("Guts boosts physical damage x1.5 while statused and ignores the burn halving",
          "[phase8][ability]") {
  DataLoader data;
  data.loadAll(BATTLE_ENGINE_DATA_DIR);
  BattleEngine engine(data);

  auto hit = [&](Status st) {
    auto state = makeDuel(data, "machamp", {"CloseCombat"}, "snorlax", {"Growl"});
    state.teams[0][0].status = st;
    FixedRNG rng(0.99f);
    auto events = engine.resolveTurn(state, UseMove{0}, UseMove{0}, rng);
    return damageOn(events, 1);
  };

  int healthy = hit(Status::None);
  int burned = hit(Status::Burn); // Guts: x1.5, no x0.5 halving
  REQUIRE(burned > healthy);
  REQUIRE(burned >= healthy * 3 / 2 - 2);
  REQUIRE(burned <= healthy * 3 / 2 + 2);
}

TEST_CASE("Thick Fat halves incoming Fire damage", "[phase8][ability]") {
  DataLoader data;
  data.loadAll(BATTLE_ENGINE_DATA_DIR);
  BattleEngine engine(data);

  // Gengar has no STAB on either move; same power, same category, same target.
  auto hit = [&](const char *move) {
    auto state = makeDuel(data, "gengar", {move}, "snorlax", {"Growl"});
    FixedRNG rng(0.99f);
    auto events = engine.resolveTurn(state, UseMove{0}, UseMove{0}, rng);
    return damageOn(events, 1);
  };

  int fire = hit("Flamethrower");
  int water = hit("Surf");
  REQUIRE(fire < water);
  REQUIRE(fire >= water / 2 - 2);
  REQUIRE(fire <= water / 2 + 2);
}

TEST_CASE("Static paralyzes on contact only", "[phase8][ability]") {
  DataLoader data;
  data.loadAll(BATTLE_ENGINE_DATA_DIR);
  BattleEngine engine(data);

  auto attackerStatus = [&](const char *move, float unit) {
    auto state = makeDuel(data, "snorlax", {move}, "pikachu", {"Growl"});
    FixedRNG rng(unit);
    engine.resolveTurn(state, UseMove{0}, UseMove{0}, rng);
    return state.teams[0][0].status;
  };

  REQUIRE(attackerStatus("Tackle", 0.1f) == Status::Paralysis); // contact, 0.1 < 0.3
  REQUIRE(attackerStatus("Tackle", 0.99f) == Status::None);     // proc denied
  REQUIRE(attackerStatus("Earthquake", 0.1f) == Status::None);  // no contact
}

TEST_CASE("Rough Skin chips a contact attacker for 1/8 max HP", "[phase8][ability]") {
  DataLoader data;
  data.loadAll(BATTLE_ENGINE_DATA_DIR);
  BattleEngine engine(data);

  auto chip = [&](const char *move) {
    auto state = makeDuel(data, "machamp", {move}, "garchomp", {"Growl"});
    int before = state.teams[0][0].currentHp;
    FixedRNG rng(0.99f);
    engine.resolveTurn(state, UseMove{0}, UseMove{0}, rng);
    return before - state.teams[0][0].currentHp;
  };

  auto state = makeDuel(data, "machamp", {"CloseCombat"}, "garchomp", {"Growl"});
  int maxHp = state.teams[0][0].stats.hp;
  REQUIRE(chip("CloseCombat") == maxHp / 8); // contact
  REQUIRE(chip("StoneEdge") == 0);           // no contact
}

TEST_CASE("Speed ties are broken by the RNG (phase 0 debt resolved)", "[phase8][order]") {
  DataLoader data;
  data.loadAll(BATTLE_ENGINE_DATA_DIR);
  BattleEngine engine(data);

  auto firstMover = [&](float unit) {
    auto state = makeDuel(data, "snorlax", {"Growl"}, "snorlax", {"Growl"});
    FixedRNG rng(unit);
    auto events = engine.resolveTurn(state, UseMove{0}, UseMove{0}, rng);
    for (const auto &ev : events)
      if (auto *e = std::get_if<MoveUsedEvent>(&ev))
        return e->user.side;
    return -1;
  };

  REQUIRE(firstMover(0.4f) == 0); // chance(0.5) hits: side 0 first
  REQUIRE(firstMover(0.6f) == 1); // chance(0.5) misses: side 1 first
}

TEST_CASE("validateState checks the new volatile fields", "[phase8][validate]") {
  DataLoader data;
  data.loadAll(BATTLE_ENGINE_DATA_DIR);

  auto state = makeDuel(data, "snorlax", {"Tackle"}, "machamp", {"CloseCombat"});
  REQUIRE_NOTHROW(validateState(state, data));

  SECTION("self-inflicted sleep flag without sleep") {
    state.teams[0][0].sleep_self_inflicted = 1;
    REQUIRE_THROWS_AS(validateState(state, data), std::invalid_argument);
  }
  SECTION("invulnerable without a charging move") {
    state.teams[0][0].invulnerable_state = 1;
    REQUIRE_THROWS_AS(validateState(state, data), std::invalid_argument);
  }
  SECTION("invalid charging move id") {
    state.teams[0][0].charging_move_id = 999;
    REQUIRE_THROWS_AS(validateState(state, data), std::invalid_argument);
  }
  SECTION("valid charge + invulnerability") {
    state.teams[0][0].charging_move_id = 0;
    state.teams[0][0].invulnerable_state = 2;
    REQUIRE_NOTHROW(validateState(state, data));
  }
}
