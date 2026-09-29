// Canon edge cases found by the pre-submission audit. Each case failed
// before its fix.
#include "helpers.hpp"
#include "engine/core/battle_state.hpp"
#include "engine/core/data_loader.hpp"
#include "engine/core/engine.hpp"
#include "engine/core/rng.hpp"
#include "engine/items/item.hpp"
#include <catch2/catch_test_macros.hpp>
#include <variant>

using namespace engine;
using engine::test::buildCombatant;

namespace {

struct Duel {
  DataLoader data;
  Duel() { engine::test::loadAll(data); }
  BattleState make(const char *a, std::vector<std::string> am, const char *b,
                   std::vector<std::string> bm) {
    BattleState s;
    s.teams[0][0] = buildCombatant(data, a, am);
    s.teams[1][0] = buildCombatant(data, b, bm);
    s.team_size = {1, 1};
    return s;
  }
};

template <typename E> bool hasEventFor(const EventLog &events, int side) {
  for (const auto &ev : events)
    if (const auto *e = std::get_if<E>(&ev))
      if (e->user.side == side)
        return true;
  return false;
}

} // namespace

TEST_CASE("An immune hit does not bust Disguise", "[canon][abilities]") {
  Duel d;
  BattleEngine e(d.data);
  auto s = d.make("MegaRayquaza", {"ExtremeSpeed"}, "Mimikyu", {"Growl"});
  FixedRNG rng(0.5f);
  e.resolveTurn(s, UseMove{0}, UseMove{0}, rng);
  REQUIRE(s.teams[1][0].disguise_broken == 0);
  REQUIRE(s.teams[1][0].currentHp == s.teams[1][0].stats.hp);
}

TEST_CASE("Fixed damage busts Disguise instead of landing", "[canon][abilities]") {
  Duel d;
  BattleEngine e(d.data);
  auto s = d.make("TingLu", {"Ruination"}, "Mimikyu", {"Growl"});
  FixedRNG rng(0.5f);
  e.resolveTurn(s, UseMove{0}, UseMove{0}, rng);
  const BattlePokemon &mimikyu = s.teams[1][0];
  REQUIRE(mimikyu.disguise_broken == 1);
  REQUIRE(mimikyu.currentHp == mimikyu.stats.hp - mimikyu.stats.hp / 8); // the chip only
}

TEST_CASE("Prankster status moves fail against Dark-types", "[canon][abilities]") {
  Duel d;
  BattleEngine e(d.data);
  auto s = d.make("MegaBanette", {"WillOWisp"}, "Weavile", {"Growl"});
  FixedRNG rng(0.5f);
  auto events = e.resolveTurn(s, UseMove{0}, UseMove{0}, rng);
  REQUIRE(s.teams[1][0].status == Status::None);
  REQUIRE(hasEventFor<MoveFailedEvent>(events, 0));

  SECTION("control: the same move lands on a non-Dark target") {
    auto t = d.make("MegaBanette", {"WillOWisp"}, "Snorlax", {"Curse"});
    e.resolveTurn(t, UseMove{0}, UseMove{0}, rng);
    REQUIRE(t.teams[1][0].status == Status::Burn);
  }
}

TEST_CASE("A move called by Sleep Talk is stopped by Protect", "[canon][move_mechanics]") {
  Duel d;
  BattleEngine e(d.data);
  auto s = d.make("Snorlax", {"SleepTalk", "BodySlam"}, "Toxapex", {"BanefulBunker"});
  s.teams[0][0].status = Status::Sleep;
  s.teams[0][0].status_turns = 2;
  FixedRNG rng(0.0f);
  e.resolveTurn(s, UseMove{0}, UseMove{0}, rng);
  REQUIRE(s.teams[1][0].currentHp == s.teams[1][0].stats.hp);
}

TEST_CASE("A semi-invulnerable target dodges status moves too", "[canon][protect]") {
  Duel d;
  BattleEngine e(d.data);
  // Dragapult is already hidden by Phantom Force when Will-O-Wisp comes.
  auto s = d.make("Dragapult", {"PhantomForce"}, "MegaBanette", {"WillOWisp"});
  s.teams[0][0].charging_move_id = d.data.findMoveId("PhantomForce");
  s.teams[0][0].invulnerable_state = 3;
  FixedRNG rng(0.5f);
  e.resolveTurn(s, UseMove{0}, UseMove{0}, rng);
  REQUIRE(s.teams[0][0].status == Status::None);
}

TEST_CASE("Defog leaves the user's own Aurora Veil up", "[canon][field]") {
  Duel d;
  BattleEngine e(d.data);
  auto s = d.make("Corviknight", {"Defog"}, "Snorlax", {"Curse"});
  s.aurora_veil_turns[0] = 5;
  s.aurora_veil_turns[1] = 5;
  FixedRNG rng(0.5f);
  e.resolveTurn(s, UseMove{0}, UseMove{0}, rng);
  REQUIRE(s.aurora_veil_turns[0] == 4); // ticked once, still up
  REQUIRE(s.aurora_veil_turns[1] == 0); // the target's side is cleared
}

TEST_CASE("A flinched sleeper still ticks its sleep counter", "[canon][status]") {
  Duel d;
  BattleEngine e(d.data);
  auto s = d.make("Snorlax", {"BodySlam"}, "Maushold", {"FakeOut"});
  s.teams[0][0].status = Status::Sleep;
  s.teams[0][0].status_turns = 2;
  FixedRNG rng(0.5f);
  e.resolveTurn(s, UseMove{0}, UseMove{0}, rng);
  REQUIRE(s.teams[0][0].status_turns == 1);
}

TEST_CASE("Protect fails when the user moves last", "[canon][protect]") {
  Duel d;
  BattleEngine e(d.data);
  auto s = d.make("Toxapex", {"BanefulBunker"}, "Snorlax", {"Curse"});
  s.teams[1][1] = buildCombatant(d.data, "Blissey", {"SoftBoiled"});
  s.team_size = {1, 2};
  FixedRNG rng(0.5f);
  // The switch goes first: nothing is left for Baneful Bunker to block.
  auto events = e.resolveTurn(s, UseMove{0}, SwitchAction{1}, rng);
  REQUIRE(hasEventFor<MoveFailedEvent>(events, 0));
}

TEST_CASE("Foul Play ignores the target's Choice Band", "[canon][damage]") {
  Duel d;
  BattleEngine e(d.data);
  auto hit = [&](bool targetHoldsBand) {
    auto s = d.make("MegaSableye", {"FoulPlay"}, "Snorlax", {"Curse"});
    s.teams[1][0].item_id = targetHoldsBand ? findItemIdByName("ChoiceBand") : kNoItem;
    FixedRNG rng(0.5f);
    e.resolveTurn(s, UseMove{0}, UseMove{0}, rng);
    return s.teams[1][0].stats.hp - s.teams[1][0].currentHp;
  };
  const int plain = hit(false);
  REQUIRE(plain > 0);
  REQUIRE(hit(true) == plain);
}

TEST_CASE("A grounded Poison-type in Heavy-Duty Boots absorbs Toxic Spikes",
          "[canon][hazards]") {
  Duel d;
  BattleEngine e(d.data);
  auto s = d.make("Snorlax", {"Curse"}, "Snorlax", {"Curse"});
  s.teams[0][1] = buildCombatant(d.data, "Toxapex", {"Recover"});
  s.teams[0][1].item_id = findItemIdByName("HeavyDutyBoots");
  s.team_size = {2, 1};
  s.hazards[0].toxic_spikes = 2;
  FixedRNG rng(0.5f);
  e.resolveTurn(s, SwitchAction{1}, UseMove{0}, rng);
  REQUIRE(s.hazards[0].toxic_spikes == 0);
  REQUIRE(s.teams[0][1].status == Status::None);
}

TEST_CASE("Unaware ignores the attacker's accuracy stages", "[canon][abilities]") {
  // Every roll at its maximum: a 100-accurate move at -6 (33%) would miss.
  class HighRNG : public RNG {
  public:
    int rangeInt(int /*min*/, int max) override { return max; }
    float unit() override { return 0.99f; }
  };
  Duel d;
  BattleEngine e(d.data);
  auto s = d.make("Snorlax", {"BodySlam"}, "Quagsire", {"Growl"});
  s.teams[0][0].stat_stages[static_cast<size_t>(StatIndex::Accuracy)] = -6;
  HighRNG rng;
  e.resolveTurn(s, UseMove{0}, UseMove{0}, rng);
  REQUIRE(s.teams[1][0].currentHp < s.teams[1][0].stats.hp);

  SECTION("control: without Unaware the same roll misses") {
    auto t = d.make("Snorlax", {"BodySlam"}, "Blissey", {"SoftBoiled"});
    t.teams[0][0].stat_stages[static_cast<size_t>(StatIndex::Accuracy)] = -6;
    HighRNG rng2;
    e.resolveTurn(t, UseMove{0}, UseMove{0}, rng2);
    REQUIRE(t.teams[1][0].currentHp == t.teams[1][0].stats.hp);
  }
}
