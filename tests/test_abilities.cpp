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

// ---------------------------------------------------------------------------
// Phase 13: the roster abilities. Holders are simulated with overrideAbility
// (test-only); the real holders arrive with the roster data in phase 14.
// ---------------------------------------------------------------------------

#include "engine/core/validate.hpp"
#include "engine/items/item.hpp"

using engine::test::overrideAbility;

namespace {

BattleState duel(const DataLoader &data, const char *s0, std::vector<std::string> m0,
                 const char *s1, std::vector<std::string> m1) {
  BattleState state;
  state.teams[0][0] = engine::test::buildCombatant(data, s0, 100, m0);
  state.teams[1][0] = engine::test::buildCombatant(data, s1, 100, m1);
  state.team_size = {1, 1};
  return state;
}

int dmgOn(const EventLog &events, int side) {
  for (const auto &ev : events)
    if (auto *e = std::get_if<DamageDealtEvent>(&ev))
      if (e->target.side == side)
        return e->damage;
  return -1;
}

template <typename E> int countEv(const EventLog &events) {
  int n = 0;
  for (const auto &ev : events)
    if (std::holds_alternative<E>(ev))
      ++n;
  return n;
}

} // namespace

TEST_CASE("HugePower doubles Attack; Technician boosts weak moves only", "[abilities13]") {
  DataLoader data;
  data.loadAll(BATTLE_ENGINE_DATA_DIR);
  BattleEngine engine(data);

  auto tackle = [&](const char *ability) {
    DataLoader d;
    d.loadAll(BATTLE_ENGINE_DATA_DIR);
    if (ability)
      overrideAbility(d, "snorlax", ability);
    BattleEngine e(d);
    auto state = duel(d, "snorlax", {"Tackle", "BodySlam"}, "machamp", {"Growl"});
    FixedRNG rng(0.99f);
    auto events = e.resolveTurn(state, UseMove{0}, UseMove{0}, rng);
    return dmgOn(events, 1);
  };

  int plain = tackle(nullptr);
  REQUIRE(tackle("HugePower") > static_cast<int>(static_cast<float>(plain) * 1.9f));
  REQUIRE(tackle("Technician") > static_cast<int>(static_cast<float>(plain) * 1.4f));

  // Body Slam (85 BP) is above the Technician cutoff.
  auto bodySlam = [&](const char *ability) {
    DataLoader d;
    d.loadAll(BATTLE_ENGINE_DATA_DIR);
    if (ability)
      overrideAbility(d, "snorlax", ability);
    BattleEngine e(d);
    auto state = duel(d, "snorlax", {"BodySlam"}, "machamp", {"Growl"});
    FixedRNG rng(0.99f);
    auto events = e.resolveTurn(state, UseMove{0}, UseMove{0}, rng);
    return dmgOn(events, 1);
  };
  REQUIRE(bodySlam("Technician") == bodySlam(nullptr));
}

TEST_CASE("Adaptability turns STAB into x2; Sharpness rewards slicing moves", "[abilities13]") {
  DataLoader data;
  data.loadAll(BATTLE_ENGINE_DATA_DIR);

  auto hit = [&](const char *attacker, const char *move, const char *ability) {
    DataLoader d;
    d.loadAll(BATTLE_ENGINE_DATA_DIR);
    if (ability)
      overrideAbility(d, attacker, ability);
    BattleEngine e(d);
    auto state = duel(d, attacker, {move}, "machamp", {"Growl"});
    FixedRNG rng(0.99f);
    auto events = e.resolveTurn(state, UseMove{0}, UseMove{0}, rng);
    return dmgOn(events, 1);
  };

  // Snorlax Tackle is STAB (Normal/Normal): 1.5 -> 2.0 is a x1.33 bump.
  int stabPlain = hit("snorlax", "Tackle", nullptr);
  int stabAdapt = hit("snorlax", "Tackle", "Adaptability");
  REQUIRE(stabAdapt > static_cast<int>(static_cast<float>(stabPlain) * 1.25f));

  // Charizard AirSlash carries the slicing flag.
  int slashPlain = hit("charizard", "AirSlash", nullptr);
  int slashSharp = hit("charizard", "AirSlash", "Sharpness");
  REQUIRE(slashSharp > static_cast<int>(static_cast<float>(slashPlain) * 1.4f));
}

TEST_CASE("Unaware ignores the other side's stages, both ways", "[abilities13]") {
  DataLoader data;
  data.loadAll(BATTLE_ENGINE_DATA_DIR);

  auto boostedTackle = [&](const char *defenderAbility) {
    DataLoader d;
    d.loadAll(BATTLE_ENGINE_DATA_DIR);
    if (defenderAbility)
      overrideAbility(d, "machamp", defenderAbility);
    BattleEngine e(d);
    auto state = duel(d, "snorlax", {"Tackle"}, "machamp", {"Growl"});
    state.teams[0][0].stat_stages[static_cast<size_t>(StatIndex::Atk)] = 6;
    FixedRNG rng(0.99f);
    auto events = e.resolveTurn(state, UseMove{0}, UseMove{0}, rng);
    return dmgOn(events, 1);
  };

  REQUIRE(boostedTackle("Unaware") < boostedTackle(nullptr) / 3); // +6 wiped
}

TEST_CASE("VesselOfRuin drains the opponent's Special Attack", "[abilities13]") {
  // Baseline "Pressure" (inerte en dégâts): Snorlax's natural ThickFat
  // would halve Fire and poison the comparison.
  auto flame = [&](const char *defenderAbility) {
    DataLoader d;
    d.loadAll(BATTLE_ENGINE_DATA_DIR);
    overrideAbility(d, "snorlax", defenderAbility ? defenderAbility : "Pressure");
    BattleEngine e(d);
    auto state = duel(d, "charizard", {"Flamethrower", "Tackle"}, "snorlax", {"Growl"});
    FixedRNG rng(0.99f);
    auto events = e.resolveTurn(state, UseMove{0}, UseMove{0}, rng);
    return dmgOn(events, 1);
  };
  int plain = flame(nullptr);
  int drained = flame("VesselOfRuin");

  REQUIRE(drained < plain);
  REQUIRE(drained > static_cast<int>(static_cast<float>(plain) * 0.65f));
}

TEST_CASE("VoltAbsorb and LightningRod void Electric moves with their perks", "[abilities13]") {
  DataLoader data;
  data.loadAll(BATTLE_ENGINE_DATA_DIR);
  overrideAbility(data, "snorlax", "VoltAbsorb");
  BattleEngine engine(data);

  auto state = duel(data, "pikachu", {"Thunderbolt"}, "snorlax", {"Growl"});
  state.teams[1][0].currentHp = state.teams[1][0].stats.hp / 2;
  FixedRNG rng(0.99f);
  auto events = engine.resolveTurn(state, UseMove{0}, UseMove{0}, rng);
  REQUIRE(dmgOn(events, 1) == -1); // absorbed
  REQUIRE(state.teams[1][0].currentHp ==
          state.teams[1][0].stats.hp / 2 + state.teams[1][0].stats.hp / 4);

  DataLoader d2;
  d2.loadAll(BATTLE_ENGINE_DATA_DIR);
  overrideAbility(d2, "snorlax", "LightningRod");
  BattleEngine e2(d2);
  auto s2 = duel(d2, "pikachu", {"Thunderbolt"}, "snorlax", {"Growl"});
  e2.resolveTurn(s2, UseMove{0}, UseMove{0}, rng);
  REQUIRE(s2.teams[1][0].stat_stages[static_cast<size_t>(StatIndex::SpA)] == 1);
}

TEST_CASE("FlashFire: Fire immunity, then a x1.5 boost that dies on switch-out", "[abilities13]") {
  DataLoader data;
  data.loadAll(BATTLE_ENGINE_DATA_DIR);
  overrideAbility(data, "charizard", "FlashFire");
  BattleEngine engine(data);

  BattleState state;
  state.teams[0][0] = engine::test::buildCombatant(data, "charizard", 100, {"Flamethrower"});
  state.teams[0][1] = engine::test::buildCombatant(data, "snorlax", 100, {"Tackle"});
  state.teams[1][0] = engine::test::buildCombatant(data, "venusaur", 100, {"Flamethrower"});
  state.team_size = {2, 1};
  FixedRNG rng(0.99f);

  // Venusaur is slower: Charizard hits unlit first, absorbs afterwards.
  auto t1 = engine.resolveTurn(state, UseMove{0}, UseMove{0}, rng);
  int unlit = dmgOn(t1, 1);
  REQUIRE(state.teams[0][0].currentHp == state.teams[0][0].stats.hp); // absorbed
  REQUIRE(state.teams[0][0].flash_fire_active == 1);

  state.teams[1][0].currentHp = state.teams[1][0].stats.hp;
  auto t2 = engine.resolveTurn(state, UseMove{0}, UseMove{0}, rng);
  REQUIRE(dmgOn(t2, 1) > static_cast<int>(static_cast<float>(unlit) * 1.4f));

  state.teams[1][0].currentHp = state.teams[1][0].stats.hp; // survived the boosted hit
  engine.resolveTurn(state, SwitchAction{1}, UseMove{0}, rng);
  REQUIRE(state.teams[0][0].flash_fire_active == 0); // the flame goes out
}

TEST_CASE("Bulletproof voids ballistic moves only", "[abilities13]") {
  DataLoader data;
  data.loadAll(BATTLE_ENGINE_DATA_DIR);
  overrideAbility(data, "machamp", "Bulletproof");
  BattleEngine engine(data);

  auto state = duel(data, "gengar", {"ShadowBall", "Tackle"}, "machamp", {"Growl"});
  FixedRNG rng(0.99f);
  auto e1 = engine.resolveTurn(state, UseMove{0}, UseMove{0}, rng);
  REQUIRE(dmgOn(e1, 1) == -1); // Ball'Ombre bounces off
  auto e2 = engine.resolveTurn(state, UseMove{1}, UseMove{0}, rng);
  REQUIRE(dmgOn(e2, 1) > 0);
}

TEST_CASE("ClearBody blocks opposing drops; Defiant answers them with +2 Atk", "[abilities13]") {
  DataLoader data;
  data.loadAll(BATTLE_ENGINE_DATA_DIR);
  overrideAbility(data, "snorlax", "ClearBody");
  BattleEngine engine(data);

  auto state = duel(data, "machamp", {"Growl"}, "snorlax", {"Tackle"});
  FixedRNG rng(0.99f);
  engine.resolveTurn(state, UseMove{0}, UseMove{0}, rng);
  REQUIRE(state.teams[1][0].stat_stages[static_cast<size_t>(StatIndex::Atk)] == 0);

  DataLoader d2;
  d2.loadAll(BATTLE_ENGINE_DATA_DIR);
  overrideAbility(d2, "snorlax", "Defiant");
  BattleEngine e2(d2);
  auto s2 = duel(d2, "machamp", {"Growl"}, "snorlax", {"Tackle"});
  e2.resolveTurn(s2, UseMove{0}, UseMove{0}, rng);
  // -1 from Growl, +2 from Defiant: net +1.
  REQUIRE(s2.teams[1][0].stat_stages[static_cast<size_t>(StatIndex::Atk)] == 1);

  // Own drops (Close Combat's) never trigger it.
  DataLoader d3;
  d3.loadAll(BATTLE_ENGINE_DATA_DIR);
  overrideAbility(d3, "machamp", "Defiant");
  BattleEngine e3(d3);
  auto s3 = duel(d3, "machamp", {"CloseCombat"}, "snorlax", {"Growl"});
  e3.resolveTurn(s3, UseMove{0}, UseMove{0}, rng);
  REQUIRE(s3.teams[0][0].stat_stages[static_cast<size_t>(StatIndex::Atk)] < 1);
}

TEST_CASE("Moxie: +1 Atk on the KO", "[abilities13]") {
  DataLoader data;
  data.loadAll(BATTLE_ENGINE_DATA_DIR);
  overrideAbility(data, "machamp", "Moxie");
  BattleEngine engine(data);

  // SwordsDance, not Growl: Pikachu outspeeds and a Growl before the KO
  // would cancel the +1 out.
  auto state = duel(data, "machamp", {"CloseCombat"}, "pikachu", {"SwordsDance"});
  FixedRNG rng(0.99f);
  engine.resolveTurn(state, UseMove{0}, UseMove{0}, rng);
  REQUIRE(state.teams[1][0].isFainted());
  REQUIRE(state.teams[0][0].stat_stages[static_cast<size_t>(StatIndex::Atk)] == 1);
}

TEST_CASE("Berserk: +1 SpA when a hit drops it below half — residuals don't count",
          "[abilities13]") {
  DataLoader data;
  data.loadAll(BATTLE_ENGINE_DATA_DIR);
  overrideAbility(data, "snorlax", "Berserk");
  BattleEngine engine(data);

  // Tackle, not Close Combat: a KO never angers anyone.
  auto state = duel(data, "machamp", {"Tackle"}, "snorlax", {"Growl"});
  state.teams[1][0].currentHp = state.teams[1][0].stats.hp / 2 + 30;
  FixedRNG rng(0.99f);
  engine.resolveTurn(state, UseMove{0}, UseMove{0}, rng);
  REQUIRE(state.teams[1][0].stat_stages[static_cast<size_t>(StatIndex::SpA)] == 1);

  // Poison chip across the threshold: no anger.
  DataLoader d2;
  d2.loadAll(BATTLE_ENGINE_DATA_DIR);
  overrideAbility(d2, "snorlax", "Berserk");
  BattleEngine e2(d2);
  auto s2 = duel(d2, "snorlax", {"Growl"}, "machamp", {"SwordsDance"});
  s2.teams[0][0].status = Status::Poison;
  s2.teams[0][0].currentHp = s2.teams[0][0].stats.hp / 2 + 5;
  e2.resolveTurn(s2, UseMove{0}, UseMove{0}, rng);
  REQUIRE(s2.teams[0][0].currentHp <= s2.teams[0][0].stats.hp / 2);
  REQUIRE(s2.teams[0][0].stat_stages[static_cast<size_t>(StatIndex::SpA)] == 0);
}

TEST_CASE("EmergencyExit auto-switches below half (ADR #47 divergence)", "[abilities13]") {
  DataLoader data;
  data.loadAll(BATTLE_ENGINE_DATA_DIR);
  overrideAbility(data, "snorlax", "EmergencyExit");
  BattleEngine engine(data);

  BattleState state;
  state.teams[0][0] = engine::test::buildCombatant(data, "snorlax", 100, {"Tackle"});
  state.teams[0][1] = engine::test::buildCombatant(data, "gyarados", 100, {"Tackle"});
  state.teams[1][0] = engine::test::buildCombatant(data, "machamp", 100, {"Tackle"});
  state.team_size = {2, 1};
  state.teams[0][0].currentHp = state.teams[0][0].stats.hp / 2 + 30;

  FixedRNG rng(0.99f);
  engine.resolveTurn(state, UseMove{0}, UseMove{0}, rng);
  REQUIRE(state.activeIndex[0] == 1); // fled to Gyarados
}

TEST_CASE("Regenerator heals a third on the way out; NaturalCure purges the status",
          "[abilities13]") {
  DataLoader data;
  data.loadAll(BATTLE_ENGINE_DATA_DIR);
  overrideAbility(data, "snorlax", "Regenerator");
  BattleEngine engine(data);

  BattleState state;
  state.teams[0][0] = engine::test::buildCombatant(data, "snorlax", 100, {"Tackle"});
  state.teams[0][1] = engine::test::buildCombatant(data, "gyarados", 100, {"Tackle"});
  state.teams[1][0] = engine::test::buildCombatant(data, "machamp", 100, {"Growl"});
  state.team_size = {2, 1};
  int max = state.teams[0][0].stats.hp;
  state.teams[0][0].currentHp = max / 2;

  FixedRNG rng(0.99f);
  engine.resolveTurn(state, SwitchAction{1}, UseMove{0}, rng);
  REQUIRE(state.teams[0][0].currentHp == max / 2 + max / 3);

  DataLoader d2;
  d2.loadAll(BATTLE_ENGINE_DATA_DIR);
  overrideAbility(d2, "snorlax", "NaturalCure");
  BattleEngine e2(d2);
  BattleState s2;
  s2.teams[0][0] = engine::test::buildCombatant(d2, "snorlax", 100, {"Tackle"});
  s2.teams[0][1] = engine::test::buildCombatant(d2, "gyarados", 100, {"Tackle"});
  s2.teams[1][0] = engine::test::buildCombatant(d2, "machamp", 100, {"Growl"});
  s2.team_size = {2, 1};
  s2.teams[0][0].status = Status::Poison;
  e2.resolveTurn(s2, SwitchAction{1}, UseMove{0}, rng);
  REQUIRE(s2.teams[0][0].status == Status::None);
}

TEST_CASE("Prankster bumps Status moves a bracket up", "[abilities13]") {
  DataLoader data;
  data.loadAll(BATTLE_ENGINE_DATA_DIR);
  overrideAbility(data, "venusaur", "Prankster"); // 80 base speed vs Charizard's 100
  BattleEngine engine(data);

  auto state = duel(data, "venusaur", {"Growl"}, "charizard", {"Tackle"});
  FixedRNG rng(0.99f);
  auto events = engine.resolveTurn(state, UseMove{0}, UseMove{0}, rng);
  for (const auto &ev : events)
    if (auto *e = std::get_if<MoveUsedEvent>(&ev)) {
      REQUIRE(e->user.side == 0); // the slower prankster acts first
      break;
    }
}

TEST_CASE("MagicBounce bounces status and hazards back at the sender", "[abilities13]") {
  DataLoader data;
  data.loadAll(BATTLE_ENGINE_DATA_DIR);
  overrideAbility(data, "snorlax", "MagicBounce");
  BattleEngine engine(data);

  // Machamp, not Venusaur: a Poison-type is immune to its own bounced Toxic.
  auto state =
      duel(data, "machamp", {"Toxic", "StealthRock", "Tackle"}, "snorlax", {"SwordsDance"});
  FixedRNG rng(0.99f);

  engine.resolveTurn(state, UseMove{0}, UseMove{0}, rng);
  REQUIRE(state.teams[1][0].status == Status::None);
  REQUIRE(state.teams[0][0].status == Status::Toxic); // came right back

  engine.resolveTurn(state, UseMove{1}, UseMove{0}, rng);
  REQUIRE(state.hazards[0].stealth_rock == 1); // landed on the setter's side
  REQUIRE(state.hazards[1].stealth_rock == 0);

  auto e3 = engine.resolveTurn(state, UseMove{2}, UseMove{0}, rng);
  REQUIRE(dmgOn(e3, 1) > 0); // damaging moves never bounce
}

TEST_CASE("Pressure doubles the PP bill of moves aimed at it", "[abilities13]") {
  DataLoader data;
  data.loadAll(BATTLE_ENGINE_DATA_DIR);
  overrideAbility(data, "snorlax", "Pressure");
  BattleEngine engine(data);

  auto state = duel(data, "machamp", {"Tackle", "SwordsDance"}, "snorlax", {"Growl"});
  FixedRNG rng(0.99f);
  engine.resolveTurn(state, UseMove{0}, UseMove{0}, rng);
  REQUIRE(state.teams[0][0].pp[0] == 33); // 35 - 2

  engine.resolveTurn(state, UseMove{1}, UseMove{0}, rng);
  REQUIRE(state.teams[0][0].pp[1] == 19); // self-targeted: normal bill
}

TEST_CASE("Unnerve keeps the opposing Sitrus in its wrapper", "[abilities13]") {
  DataLoader data;
  data.loadAll(BATTLE_ENGINE_DATA_DIR);
  overrideAbility(data, "machamp", "Unnerve");
  BattleEngine engine(data);

  auto state = duel(data, "machamp", {"Tackle"}, "snorlax", {"Growl"});
  state.teams[1][0].item_id = data.findItemId("SitrusBerry");
  state.teams[1][0].currentHp = state.teams[1][0].stats.hp / 2 + 10;
  FixedRNG rng(0.99f);
  auto events = engine.resolveTurn(state, UseMove{0}, UseMove{0}, rng);
  REQUIRE(countEv<ItemConsumedEvent>(events) == 0);
  REQUIRE(state.teams[1][0].item_consumed == 0);
}

TEST_CASE("RockHead cancels recoil; Struggle's stays canon", "[abilities13]") {
  DataLoader data;
  data.loadAll(BATTLE_ENGINE_DATA_DIR);
  overrideAbility(data, "charizard", "RockHead");
  BattleEngine engine(data);

  auto state = duel(data, "charizard", {"BraveBird"}, "snorlax", {"Growl"});
  FixedRNG rng(0.99f);
  auto events = engine.resolveTurn(state, UseMove{0}, UseMove{0}, rng);
  REQUIRE(dmgOn(events, 1) > 0);
  REQUIRE(countEv<RecoilDamageEvent>(events) == 0);
  REQUIRE(state.teams[0][0].currentHp == state.teams[0][0].stats.hp);

  state.teams[0][0].pp[0] = 0; // Struggle's flat recoil is not "recoil damage"
  auto e2 = engine.resolveTurn(state, UseMove{0}, UseMove{0}, rng);
  REQUIRE(countEv<RecoilDamageEvent>(e2) == 1);
}

TEST_CASE("LeafGuard blocks status under the sun only", "[abilities13]") {
  DataLoader data;
  data.loadAll(BATTLE_ENGINE_DATA_DIR);
  overrideAbility(data, "venusaur", "LeafGuard");
  BattleEngine engine(data);

  auto state = duel(data, "charizard", {"WillOWisp"}, "venusaur", {"Growl"});
  state.weather = Weather::Sun;
  state.weather_turns_left = 5;
  FixedRNG rng(0.99f);
  engine.resolveTurn(state, UseMove{0}, UseMove{0}, rng);
  REQUIRE(state.teams[1][0].status == Status::None);

  state.weather = Weather::None;
  state.weather_turns_left = 0;
  engine.resolveTurn(state, UseMove{0}, UseMove{0}, rng);
  REQUIRE(state.teams[1][0].status == Status::Burn);
}

TEST_CASE("Disguise eats the first hit for 1/8 and stays popped", "[abilities13]") {
  DataLoader data;
  data.loadAll(BATTLE_ENGINE_DATA_DIR);
  overrideAbility(data, "pikachu", "Disguise");
  BattleEngine engine(data);

  auto state = duel(data, "machamp", {"CloseCombat"}, "pikachu", {"Growl"});
  int max = state.teams[1][0].stats.hp;
  FixedRNG rng(0.99f);

  auto e1 = engine.resolveTurn(state, UseMove{0}, UseMove{0}, rng);
  REQUIRE(state.teams[1][0].disguise_broken == 1);
  REQUIRE(state.teams[1][0].currentHp == max - max / 8); // chip only
  REQUIRE(dmgOn(e1, 1) == -1);

  auto e2 = engine.resolveTurn(state, UseMove{0}, UseMove{0}, rng);
  REQUIRE(dmgOn(e2, 1) > 0); // the costume is gone
}

TEST_CASE("Magician pockets the target's item on a hit", "[abilities13]") {
  DataLoader data;
  data.loadAll(BATTLE_ENGINE_DATA_DIR);
  overrideAbility(data, "gengar", "Magician");
  BattleEngine engine(data);

  // SludgeBomb: ShadowBall would bounce off a Normal-type for 0x.
  auto state = duel(data, "gengar", {"SludgeBomb"}, "snorlax", {"Growl"});
  state.teams[1][0].item_id = data.findItemId("Leftovers");
  FixedRNG rng(0.99f);
  auto events = engine.resolveTurn(state, UseMove{0}, UseMove{0}, rng);
  REQUIRE(countEv<ItemKnockedOffEvent>(events) == 1);
  REQUIRE(state.teams[0][0].item_id == data.findItemId("Leftovers"));
  REQUIRE(state.teams[1][0].item_id == kNoItem);
}

TEST_CASE("SnowWarning and ElectricSurge set their field on switch-in", "[abilities13]") {
  DataLoader data;
  data.loadAll(BATTLE_ENGINE_DATA_DIR);
  overrideAbility(data, "snorlax", "SnowWarning");
  overrideAbility(data, "machamp", "ElectricSurge");
  BattleEngine engine(data);

  auto state = duel(data, "snorlax", {"Tackle"}, "machamp", {"Growl"});
  FixedRNG rng(0.99f);
  auto events = engine.startBattle(state, rng);
  REQUIRE(state.weather == Weather::Snow);
  REQUIRE(state.terrain == Terrain::Electric);
  REQUIRE(countEv<TerrainStartedEvent>(events) == 1);
}

TEST_CASE("DeltaStream: presence-bound, unremplacable, shields the Flying component",
          "[abilities13]") {
  DataLoader data;
  data.loadAll(BATTLE_ENGINE_DATA_DIR);
  overrideAbility(data, "gyarados", "DeltaStream");
  BattleEngine engine(data);

  BattleState state;
  state.teams[0][0] = engine::test::buildCombatant(data, "gyarados", 100, {"Tackle"});
  state.teams[0][1] = engine::test::buildCombatant(data, "snorlax", 100, {"Tackle"});
  state.teams[1][0] =
      engine::test::buildCombatant(data, "pikachu", 100, {"Thunderbolt", "RainDance"});
  state.team_size = {2, 1};
  FixedRNG rng(0.99f);

  engine.startBattle(state, rng);
  REQUIRE(state.weather == Weather::StrongWinds);
  REQUIRE(state.weather_turns_left == 0);
  REQUIRE_NOTHROW(validateState(state, data));

  // Electric vs Water/Flying is x4; the winds drop the Flying part to x1.
  auto e1 = engine.resolveTurn(state, UseMove{0}, UseMove{0}, rng);
  int shielded = dmgOn(e1, 0);
  REQUIRE(state.weather == Weather::StrongWinds); // Rain Dance would have to wait

  auto e2 = engine.resolveTurn(state, UseMove{0}, UseMove{1}, rng);
  REQUIRE(countEv<MoveFailedEvent>(e2) == 1); // Danse Pluie fails under the winds

  engine.resolveTurn(state, SwitchAction{1}, UseMove{0}, rng);
  REQUIRE(state.weather == Weather::None); // the setter left: the winds die

  state.teams[1][0].currentHp = state.teams[1][0].stats.hp;
  engine.resolveTurn(state, SwitchAction{0}, UseMove{0}, rng); // Gyarados returns...
  REQUIRE(state.weather == Weather::StrongWinds);              // ...and so do the winds
  state.teams[0][0].currentHp = state.teams[0][0].stats.hp;
  auto e3 = engine.resolveTurn(state, UseMove{0}, UseMove{0}, rng);
  REQUIRE(dmgOn(e3, 0) == shielded); // shielded again: x4 collapsed to x2 both times
}

TEST_CASE("SlushRush doubles Speed under snow and flips the order", "[abilities13]") {
  DataLoader data;
  data.loadAll(BATTLE_ENGINE_DATA_DIR);
  overrideAbility(data, "Mamoswine", "SlushRush"); // 80 base vs Charizard's 100
  BattleEngine engine(data);

  auto first = [&](bool snow) {
    auto state = duel(data, "Mamoswine", {"Growl"}, "charizard", {"Growl"});
    if (snow) {
      state.weather = Weather::Snow;
      state.weather_turns_left = 5;
    }
    FixedRNG rng(0.99f);
    auto events = engine.resolveTurn(state, UseMove{0}, UseMove{0}, rng);
    for (const auto &ev : events)
      if (auto *e = std::get_if<MoveUsedEvent>(&ev))
        return e->user.side;
    return -1;
  };

  REQUIRE(first(false) == 1);
  REQUIRE(first(true) == 0);
}

TEST_CASE("ToughClaws boosts contact moves only", "[abilities13]") {
  auto hit = [&](const char *attacker, const char *move, bool clawed) {
    DataLoader d;
    d.loadAll(BATTLE_ENGINE_DATA_DIR);
    if (clawed)
      overrideAbility(d, attacker, "ToughClaws");
    BattleEngine e(d);
    auto state = duel(d, attacker, {move}, "machamp", {"Growl"});
    FixedRNG rng(0.99f);
    auto events = e.resolveTurn(state, UseMove{0}, UseMove{0}, rng);
    return dmgOn(events, 1);
  };

  int plain = hit("snorlax", "Tackle", false);
  REQUIRE(hit("snorlax", "Tackle", true) > static_cast<int>(static_cast<float>(plain) * 1.2f));
  // Flamethrower makes no contact: the claws stay in the pocket.
  REQUIRE(hit("charizard", "Flamethrower", true) == hit("charizard", "Flamethrower", false));
}

TEST_CASE("SwiftSwim and SandRush are wired to their own weather", "[abilities13]") {
  // Same WeatherSpeed class as SlushRush: this guards the per-instance
  // wiring (ability name -> weather), not the shared mechanism.
  auto firstMover = [&](const char *ability, Weather weather) {
    DataLoader d;
    d.loadAll(BATTLE_ENGINE_DATA_DIR);
    overrideAbility(d, "Mamoswine", ability); // 80 base speed vs Charizard's 100
    BattleEngine e(d);
    auto state = duel(d, "Mamoswine", {"Growl"}, "charizard", {"Growl"});
    state.weather = weather;
    state.weather_turns_left = 5;
    FixedRNG rng(0.99f);
    auto events = e.resolveTurn(state, UseMove{0}, UseMove{0}, rng);
    for (const auto &ev : events)
      if (auto *e2 = std::get_if<MoveUsedEvent>(&ev))
        return e2->user.side;
    return -1;
  };

  REQUIRE(firstMover("SwiftSwim", Weather::Rain) == 0);
  REQUIRE(firstMover("SwiftSwim", Weather::Sand) == 1); // wrong weather: no boost
  REQUIRE(firstMover("SandRush", Weather::Sand) == 0);
  REQUIRE(firstMover("SandRush", Weather::Rain) == 1);
}
