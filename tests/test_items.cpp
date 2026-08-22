#include "helpers.hpp"

#include "engine/core/battle_state.hpp"
#include "engine/core/data_loader.hpp"
#include "engine/core/engine.hpp"
#include "engine/core/rng.hpp"
#include "engine/core/validate.hpp"
#include "engine/items/item.hpp"
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

BattleState makeDuel(const DataLoader &data, const char *s0, std::vector<std::string> m0,
                     const char *s1, std::vector<std::string> m1) {
  BattleState state;
  state.teams[0][0] = buildCombatant(data, s0, m0);
  state.teams[1][0] = buildCombatant(data, s1, m1);
  state.team_size = {1, 1};
  return state;
}

void give(BattlePokemon &p, const DataLoader &data, const char *item) {
  p.item_id = data.findItemId(item);
  REQUIRE(p.item_id >= 0);
}

} // namespace

TEST_CASE("Item catalog: stable code-side ids, name lookup (ADR #45)", "[phase11][catalog]") {
  REQUIRE(itemCount() == 13);
  REQUIRE(findItemIdByName("LifeOrb") == 0); // frozen order: append-only
  REQUIRE(findItemIdByName("LightClay") == 12);
  REQUIRE(findItemIdByName("Restes") == -1); // the French era is over (ADR #48)
  for (int i = 0; i < itemCount(); ++i) {
    REQUIRE(itemByIndex(i) != nullptr);
    REQUIRE(findItemIdByName(itemByIndex(i)->name()) == i);
  }
  DataLoader data;
  engine::test::loadAll(data);
  REQUIRE(data.findItemId("Leftovers") == 1);
}

TEST_CASE("LifeOrb: x1.3 on damage, 10% max-HP bill after the hit", "[phase11][orbevie]") {
  DataLoader data;
  engine::test::loadAll(data);
  BattleEngine engine(data);

  auto run = [&](bool withOrb) {
    auto state = makeDuel(data, "Snorlax", {"Tackle"}, "Conkeldurr", {"Growl"});
    if (withOrb)
      give(state.teams[0][0], data, "LifeOrb");
    FixedRNG rng(0.99f);
    auto events = engine.resolveTurn(state, UseMove{0}, UseMove{0}, rng);
    return std::pair{damageOn(events, 1), state.teams[0][0].stats.hp - state.teams[0][0].currentHp};
  };

  auto [plainDmg, plainSelf] = run(false);
  auto [orbDmg, orbSelf] = run(true);
  REQUIRE(orbDmg > static_cast<int>(static_cast<float>(plainDmg) * 1.25f));
  REQUIRE(plainSelf == 0);
  auto state = makeDuel(data, "Snorlax", {"Tackle"}, "Conkeldurr", {"Growl"});
  REQUIRE(orbSelf == state.teams[0][0].stats.hp / 10);
}

TEST_CASE("LifeOrb stays quiet on a status move", "[phase11][orbevie]") {
  DataLoader data;
  engine::test::loadAll(data);
  BattleEngine engine(data);

  auto state = makeDuel(data, "Snorlax", {"Growl"}, "Conkeldurr", {"SwordsDance"});
  give(state.teams[0][0], data, "LifeOrb");
  FixedRNG rng(0.99f);
  engine.resolveTurn(state, UseMove{0}, UseMove{0}, rng);
  REQUIRE(state.teams[0][0].currentHp == state.teams[0][0].stats.hp);
}

TEST_CASE("Leftovers heals 1/16 in the residual window, before the poison ticks",
          "[phase11][restes]") {
  DataLoader data;
  engine::test::loadAll(data);
  BattleEngine engine(data);

  auto state = makeDuel(data, "Snorlax", {"Growl"}, "Conkeldurr", {"SwordsDance"});
  give(state.teams[0][0], data, "Leftovers");
  BattlePokemon &lax = state.teams[0][0];
  lax.currentHp = lax.stats.hp - 100;
  lax.status = Status::Poison;

  FixedRNG rng(0.99f);
  auto events = engine.resolveTurn(state, UseMove{0}, UseMove{0}, rng);

  int healIdx = -1, poisonIdx = -1;
  for (size_t i = 0; i < events.size(); ++i) {
    if (std::holds_alternative<HealedEvent>(events[i]))
      healIdx = static_cast<int>(i);
    if (std::holds_alternative<StatusDamageEvent>(events[i]))
      poisonIdx = static_cast<int>(i);
  }
  REQUIRE(healIdx >= 0);
  REQUIRE(poisonIdx > healIdx); // canon: Leftovers before the status damage
  // net: +hp/16 then -hp/8
  REQUIRE(lax.currentHp == lax.stats.hp - 100 + lax.stats.hp / 16 - lax.stats.hp / 8);
}

TEST_CASE("BlackSludge: heals its Poison-type holder, hurts anyone else", "[phase11][detritus]") {
  DataLoader data;
  engine::test::loadAll(data);
  BattleEngine engine(data);

  // Gengar is Ghost/Poison: 1/16 heal.
  auto state = makeDuel(data, "MegaGengar", {"SwordsDance"}, "Conkeldurr", {"Growl"});
  give(state.teams[0][0], data, "BlackSludge");
  state.teams[0][0].currentHp -= 100;
  FixedRNG rng(0.99f);
  engine.resolveTurn(state, UseMove{0}, UseMove{0}, rng);
  REQUIRE(state.teams[0][0].currentHp ==
          state.teams[0][0].stats.hp - 100 + state.teams[0][0].stats.hp / 16);

  // Snorlax is not: 1/8 chip.
  auto state2 = makeDuel(data, "Snorlax", {"Growl"}, "Conkeldurr", {"SwordsDance"});
  give(state2.teams[0][0], data, "BlackSludge");
  auto events = engine.resolveTurn(state2, UseMove{0}, UseMove{0}, rng);
  REQUIRE(countEvents<ItemDamageEvent>(events) == 1);
  REQUIRE(state2.teams[0][0].currentHp ==
          state2.teams[0][0].stats.hp - state2.teams[0][0].stats.hp / 8);
}

TEST_CASE("FlameOrb burns its holder after the residuals (no tick that turn)",
          "[phase11][orbeflamme]") {
  DataLoader data;
  engine::test::loadAll(data);
  BattleEngine engine(data);

  auto state = makeDuel(data, "Conkeldurr", {"SwordsDance"}, "Snorlax", {"Growl"});
  give(state.teams[0][0], data, "FlameOrb");

  FixedRNG rng(0.99f);
  auto t1 = engine.resolveTurn(state, UseMove{0}, UseMove{0}, rng);
  REQUIRE(state.teams[0][0].status == Status::Burn);
  REQUIRE(countEvents<StatusDamageEvent>(t1) == 0); // activates last: burn ticks next turn
  REQUIRE(state.teams[0][0].currentHp == state.teams[0][0].stats.hp);

  auto t2 = engine.resolveTurn(state, UseMove{0}, UseMove{0}, rng);
  REQUIRE(countEvents<StatusDamageEvent>(t2) == 1);
}

TEST_CASE("FlameOrb respects type immunity and existing statuses", "[phase11][orbeflamme]") {
  DataLoader data;
  engine::test::loadAll(data);
  BattleEngine engine(data);

  // Fire-type: never burned.
  auto state = makeDuel(data, "Infernape", {"SwordsDance"}, "Snorlax", {"Growl"});
  give(state.teams[0][0], data, "FlameOrb");
  FixedRNG rng(0.99f);
  engine.resolveTurn(state, UseMove{0}, UseMove{0}, rng);
  REQUIRE(state.teams[0][0].status == Status::None);

  // Already statused: the orb stays quiet.
  auto state2 = makeDuel(data, "Conkeldurr", {"SwordsDance"}, "Snorlax", {"Growl"});
  give(state2.teams[0][0], data, "FlameOrb");
  state2.teams[0][0].status = Status::Paralysis;
  engine.resolveTurn(state2, UseMove{0}, UseMove{0}, rng);
  REQUIRE(state2.teams[0][0].status == Status::Paralysis);
}

TEST_CASE("SitrusBerry pops when crossing 50%, heals 25%, once", "[phase11][sitrus]") {
  DataLoader data;
  engine::test::loadAll(data);
  BattleEngine engine(data);

  auto state = makeDuel(data, "Snorlax", {"Growl"}, "Conkeldurr", {"Tackle"});
  give(state.teams[0][0], data, "SitrusBerry");
  BattlePokemon &lax = state.teams[0][0];
  lax.currentHp = lax.stats.hp / 2 + 20; // just above the threshold

  FixedRNG rng(0.99f);
  auto events = engine.resolveTurn(state, UseMove{0}, UseMove{0}, rng);
  int dealt = damageOn(events, 0);
  REQUIRE(dealt > 20);

  REQUIRE(countEvents<ItemConsumedEvent>(events) == 1);
  REQUIRE(lax.item_consumed == 1);
  REQUIRE(lax.currentHp == lax.stats.hp / 2 + 20 - dealt + lax.stats.hp / 4);

  // Eaten: a second crossing does nothing.
  lax.currentHp = lax.stats.hp / 2 + 20;
  auto t2 = engine.resolveTurn(state, UseMove{0}, UseMove{0}, rng);
  REQUIRE(countEvents<ItemConsumedEvent>(t2) == 0);
}

TEST_CASE("FocusSash: survives a lethal hit at 1 HP, from full HP only", "[phase11][ceinture]") {
  DataLoader data;
  engine::test::loadAll(data);
  BattleEngine engine(data);

  // Conkeldurr Close Combat one-shots Weavile (Dark/Ice: 4x) from full.
  auto state = makeDuel(data, "Weavile", {"Growl"}, "Conkeldurr", {"CloseCombat"});
  give(state.teams[0][0], data, "FocusSash");
  FixedRNG rng(0.99f);
  auto events = engine.resolveTurn(state, UseMove{0}, UseMove{0}, rng);
  REQUIRE(state.teams[0][0].currentHp == 1);
  REQUIRE(state.teams[0][0].item_consumed == 1);
  REQUIRE(countEvents<ItemConsumedEvent>(events) == 1);

  // Chipped beforehand: the sash stays quiet and the holder goes down.
  auto state2 = makeDuel(data, "Weavile", {"Growl"}, "Conkeldurr", {"CloseCombat"});
  give(state2.teams[0][0], data, "FocusSash");
  state2.teams[0][0].currentHp -= 1;
  engine.resolveTurn(state2, UseMove{0}, UseMove{0}, rng);
  REQUIRE(state2.teams[0][0].isFainted());
  REQUIRE(state2.teams[0][0].item_consumed == 0);
}

TEST_CASE("Choice items: x1.5 on their stat and a lock on the first move used",
          "[phase11][choix]") {
  DataLoader data;
  engine::test::loadAll(data);
  BattleEngine engine(data);

  auto run = [&](bool withBand) {
    // Tackle: the banded hit must not KO, or the lock assertions below
    // would test a finished battle.
    auto state = makeDuel(data, "Conkeldurr", {"Tackle", "CloseCombat"}, "Snorlax", {"Growl"});
    if (withBand)
      give(state.teams[0][0], data, "ChoiceBand");
    FixedRNG rng(0.99f);
    auto events = engine.resolveTurn(state, UseMove{0}, UseMove{0}, rng);
    return std::pair{damageOn(events, 1), std::move(state)};
  };

  auto [plain, s1] = run(false);
  auto [banded, s2] = run(true);
  REQUIRE(banded > static_cast<int>(static_cast<float>(plain) * 1.4f));

  REQUIRE(s2.teams[0][0].locked_move_id == s2.teams[0][0].move_ids[0]);
  FixedRNG rng(0.99f);
  REQUIRE_THROWS_AS(engine.resolveTurn(s2, UseMove{1}, UseMove{0}, rng), std::invalid_argument);
  REQUIRE_NOTHROW(engine.resolveTurn(s2, UseMove{0}, UseMove{0}, rng));
}

TEST_CASE("The Choice lock ends on switch-out", "[phase11][choix]") {
  DataLoader data;
  engine::test::loadAll(data);
  BattleEngine engine(data);

  BattleState state;
  state.teams[0][0] = buildCombatant(data, "Conkeldurr", {"CloseCombat", "Tackle"});
  state.teams[0][1] = buildCombatant(data, "Snorlax", {"BodySlam"});
  state.teams[1][0] = buildCombatant(data, "Gyarados", {"Growl"});
  state.team_size = {2, 1};
  give(state.teams[0][0], data, "ChoiceBand");

  FixedRNG rng(0.99f);
  engine.resolveTurn(state, UseMove{0}, UseMove{0}, rng);
  REQUIRE(state.teams[0][0].locked_move_id != kNoMove);
  engine.resolveTurn(state, SwitchAction{1}, UseMove{0}, rng);
  REQUIRE(state.teams[0][0].locked_move_id == kNoMove);
}

TEST_CASE("ChoiceScarf flips the turn order", "[phase11][choix]") {
  DataLoader data;
  engine::test::loadAll(data);
  BattleEngine engine(data);

  auto firstMover = [&](bool withScarf) {
    auto state = makeDuel(data, "Excadrill", {"Growl"}, "Infernape", {"Growl"});
    if (withScarf)
      give(state.teams[0][0], data, "ChoiceScarf");
    FixedRNG rng(0.99f);
    auto events = engine.resolveTurn(state, UseMove{0}, UseMove{0}, rng);
    for (const auto &ev : events)
      if (auto *e = std::get_if<MoveUsedEvent>(&ev))
        return e->user.side;
    return -1;
  };

  REQUIRE(firstMover(false) == 1); // Infernape (346) outruns Excadrill (275)
  REQUIRE(firstMover(true) == 0);  // ...until the scarf (x1.5 -> 412) flips it
}

TEST_CASE("Locked into a dry slot: Struggle takes over", "[phase11][choix]") {
  DataLoader data;
  engine::test::loadAll(data);
  BattleEngine engine(data);

  auto state = makeDuel(data, "Conkeldurr", {"CloseCombat", "Tackle"}, "Snorlax", {"Growl"});
  give(state.teams[0][0], data, "ChoiceBand");
  state.teams[0][0].locked_move_id = state.teams[0][0].move_ids[0];
  state.teams[0][0].pp[0] = 0; // dry locked slot; Tackle still has PP

  FixedRNG rng(0.99f);
  auto events = engine.resolveTurn(state, UseMove{0}, UseMove{0}, rng);
  bool usedStruggle = false;
  for (const auto &ev : events)
    if (auto *e = std::get_if<MoveUsedEvent>(&ev))
      if (e->user.side == 0 && e->moveName == "Struggle")
        usedStruggle = true;
  REQUIRE(usedStruggle);
  REQUIRE(state.teams[0][0].pp[1] == 35);
}

TEST_CASE("ThickClub doubles the Attack stat", "[phase11][massue]") {
  DataLoader data;
  engine::test::loadAll(data);
  BattleEngine engine(data);

  auto hit = [&](bool withClub) {
    auto state = makeDuel(data, "Snorlax", {"Tackle"}, "Conkeldurr", {"Growl"});
    if (withClub)
      give(state.teams[0][0], data, "ThickClub");
    FixedRNG rng(0.99f);
    auto events = engine.resolveTurn(state, UseMove{0}, UseMove{0}, rng);
    return damageOn(events, 1);
  };

  int plain = hit(false);
  int clubbed = hit(true);
  REQUIRE(clubbed > static_cast<int>(static_cast<float>(plain) * 1.9f));
}

TEST_CASE("HeavyDutyBoots: entry hazards don't apply at all", "[phase11][bottes]") {
  DataLoader data;
  engine::test::loadAll(data);
  BattleEngine engine(data);

  auto run = [&](bool withBoots) {
    BattleState state;
    state.teams[0][0] = buildCombatant(data, "Gyarados", {"Growl"});
    state.teams[0][1] = buildCombatant(data, "Excadrill", {"Tackle"}); // grounded
    state.teams[1][0] = buildCombatant(data, "Aerodactyl", {"StealthRock", "Spikes"});
    state.team_size = {2, 1};
    if (withBoots)
      give(state.teams[0][1], data, "HeavyDutyBoots");
    FixedRNG rng(0.99f);
    engine.resolveTurn(state, UseMove{0}, UseMove{0}, rng);
    engine.resolveTurn(state, UseMove{0}, UseMove{1}, rng);
    auto events = engine.resolveTurn(state, SwitchAction{1}, UseMove{0}, rng);
    return countEvents<HazardDamageEvent>(events);
  };

  REQUIRE(run(false) == 2);
  REQUIRE(run(true) == 0);
}

TEST_CASE("validateState checks the item invariants", "[phase11][validate]") {
  DataLoader data;
  engine::test::loadAll(data);

  auto state = makeDuel(data, "Snorlax", {"Tackle"}, "Conkeldurr", {"Growl"});
  give(state.teams[0][0], data, "Leftovers");
  REQUIRE_NOTHROW(validateState(state, data));

  state.teams[0][0].item_id = 999;
  REQUIRE_THROWS_AS(validateState(state, data), std::invalid_argument);
  state.teams[0][0].item_id = kNoItem;

  state.teams[0][0].item_consumed = 1; // consumed without an item
  REQUIRE_THROWS_AS(validateState(state, data), std::invalid_argument);
  state.teams[0][0].item_consumed = 0;

  state.teams[0][0].locked_move_id = 9999; // matches none of the moves
  REQUIRE_THROWS_AS(validateState(state, data), std::invalid_argument);
}

TEST_CASE("Integration: Cran + FlameOrb, the Betochef combo on Machamp", "[phase11][integration]") {
  DataLoader data;
  engine::test::loadAll(data);
  BattleEngine engine(data);

  // Turn 1: the orb burns Machamp (Guts holder) at the end of the turn.
  // Turn 2: burned Guts hits x1.5 with no physical burn penalty. Tackle,
  // not Close Combat: a turn-1 KO would end the battle before the item
  // window even opens.
  auto damageOnTurn2 = [&](bool withOrb) {
    auto state = makeDuel(data, "Conkeldurr", {"Tackle"}, "Snorlax", {"Growl"});
    if (withOrb)
      give(state.teams[0][0], data, "FlameOrb");
    FixedRNG rng(0.99f);
    engine.resolveTurn(state, UseMove{0}, UseMove{0}, rng);
    state.teams[1][0].currentHp = state.teams[1][0].stats.hp;
    state.teams[1][0].stat_stages = {};
    state.teams[0][0].stat_stages = {};
    auto events = engine.resolveTurn(state, UseMove{0}, UseMove{0}, rng);
    return damageOn(events, 1);
  };

  int plain = damageOnTurn2(false);
  int gutsy = damageOnTurn2(true);
  REQUIRE(gutsy > static_cast<int>(static_cast<float>(plain) * 1.4f)); // x1.5, no halving
}
