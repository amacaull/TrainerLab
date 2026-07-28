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
using engine::test::overrideAbility;

namespace {

BattleState duel(const DataLoader &data, const char *s0, std::vector<std::string> m0,
                 const char *s1, std::vector<std::string> m1) {
  BattleState state;
  state.teams[0][0] = buildCombatant(data, s0, 100, m0);
  state.teams[1][0] = buildCombatant(data, s1, 100, m1);
  state.team_size = {1, 1};
  return state;
}

template <typename E> int countEv(const EventLog &events) {
  int n = 0;
  for (const auto &ev : events)
    if (std::holds_alternative<E>(ev))
      ++n;
  return n;
}

int hitsOn(const EventLog &events, int side) {
  int n = 0;
  for (const auto &ev : events)
    if (auto *e = std::get_if<DamageDealtEvent>(&ev))
      if (e->target.side == side && e->damage > 0)
        ++n;
  return n;
}

int dmgOn(const EventLog &events, int side, int nth = 0) {
  for (const auto &ev : events)
    if (auto *e = std::get_if<DamageDealtEvent>(&ev))
      if (e->target.side == side && nth-- == 0)
        return e->damage;
  return -1;
}

class MissRNG : public RNG {
public:
  int rangeInt(int /*min*/, int max) override { return max; }
  float unit() override { return 0.99f; }
};

} // namespace

TEST_CASE("MultiHit: 2-5 rolls low without dice, LoadedDice raises the floor", "[mech]") {
  DataLoader data;
  engine::test::loadAll(data);
  BattleEngine engine(data);

  auto volley = [&](bool dice) {
    auto state = duel(data, "Toxapex", {"BulletSeed"}, "Snorlax", {"Growl"});
    if (dice)
      state.teams[0][0].item_id = data.findItemId("LoadedDice");
    FixedRNG rng(0.99f); // rangeInt -> min: the 2-5 roll bottoms out at 2
    auto events = engine.resolveTurn(state, UseMove{0}, UseMove{0}, rng);
    return hitsOn(events, 1);
  };

  REQUIRE(volley(false) == 2);
  REQUIRE(volley(true) == 4); // floor raised, unit 0.99 refuses the 5th
}

TEST_CASE("PopulationBomb lands its 10 hits; TripleAxel escalates; DragonDarts is 2", "[mech]") {
  DataLoader data;
  engine::test::loadAll(data);
  BattleEngine engine(data);
  FixedRNG rng(0.5f); // rangeInt -> min: every per-hit retest passes

  auto s1 = duel(data, "Luxray", {"PopulationBomb"}, "Snorlax", {"Growl"});
  REQUIRE(hitsOn(engine.resolveTurn(s1, UseMove{0}, UseMove{0}, rng), 1) == 10);

  auto s2 = duel(data, "Mamoswine", {"TripleAxel"}, "Snorlax", {"Growl"});
  auto e2 = engine.resolveTurn(s2, UseMove{0}, UseMove{0}, rng);
  REQUIRE(hitsOn(e2, 1) == 3);
  int h1 = dmgOn(e2, 1, 0), h2 = dmgOn(e2, 1, 1), h3 = dmgOn(e2, 1, 2);
  REQUIRE(h1 < h2);
  REQUIRE(h2 < h3); // 20 / 40 / 60 base power ladder

  auto s3 = duel(data, "Excadrill", {"DragonDarts"}, "Snorlax", {"Growl"});
  REQUIRE(hitsOn(engine.resolveTurn(s3, UseMove{0}, UseMove{0}, rng), 1) == 2);
}

TEST_CASE("Disguise eats one hit of a volley, the rest lands (canon gen 8)", "[mech]") {
  DataLoader data;
  engine::test::loadAll(data);
  overrideAbility(data, "Luxray", "Disguise");
  BattleEngine engine(data);

  auto state = duel(data, "Toxapex", {"BulletSeed"}, "Luxray", {"Growl"});
  FixedRNG rng(0.99f); // 2 hits
  auto events = engine.resolveTurn(state, UseMove{0}, UseMove{0}, rng);
  REQUIRE(state.teams[1][0].disguise_broken == 1);
  REQUIRE(hitsOn(events, 1) == 1); // hit 1 popped the costume, hit 2 landed
}

TEST_CASE("KnockOff: x1.5 with something to steal, then the item is gone", "[mech]") {
  DataLoader data;
  engine::test::loadAll(data);
  BattleEngine engine(data);

  auto hit = [&](bool holder) {
    auto state = duel(data, "Conkeldurr", {"KnockOff"}, "Snorlax", {"Growl"});
    if (holder)
      state.teams[1][0].item_id = data.findItemId("Leftovers");
    FixedRNG rng(0.99f);
    auto events = engine.resolveTurn(state, UseMove{0}, UseMove{0}, rng);
    return std::tuple{dmgOn(events, 1), countEv<ItemKnockedOffEvent>(events),
                      state.teams[1][0].item_id};
  };

  auto [dPlain, evPlain, itemPlain] = hit(false);
  auto [dBoost, evBoost, itemBoost] = hit(true);
  REQUIRE(dBoost > static_cast<int>(static_cast<float>(dPlain) * 1.4f));
  REQUIRE(evPlain == 0);
  REQUIRE(evBoost == 1);
  REQUIRE(itemBoost == kNoItem);
  (void)itemPlain;
}

TEST_CASE("Drain heals half the damage; IronFist finally has its punch", "[mech]") {
  DataLoader data;
  engine::test::loadAll(data);
  BattleEngine engine(data);

  auto state = duel(data, "Conkeldurr", {"DrainPunch"}, "Snorlax", {"Growl"});
  state.teams[0][0].currentHp = 100;
  FixedRNG rng(0.99f);
  auto events = engine.resolveTurn(state, UseMove{0}, UseMove{0}, rng);
  int dealt = dmgOn(events, 1);
  REQUIRE(state.teams[0][0].currentHp == 100 + dealt / 2);

  auto punch = [&](const char *ability) {
    DataLoader d;
    engine::test::loadAll(d);
    if (ability)
      overrideAbility(d, "Conkeldurr", ability);
    BattleEngine e(d);
    auto s = duel(d, "Conkeldurr", {"MachPunch"}, "Snorlax", {"Growl"});
    FixedRNG r(0.99f);
    return dmgOn(e.resolveTurn(s, UseMove{0}, UseMove{0}, r), 1);
  };
  REQUIRE(punch("IronFist") > static_cast<int>(static_cast<float>(punch(nullptr)) * 1.15f));
}

TEST_CASE("FixedDamage: SeismicToss deals exactly the level, Ghosts shrug it off", "[mech]") {
  DataLoader data;
  engine::test::loadAll(data);
  BattleEngine engine(data);
  FixedRNG rng(0.99f);

  auto s1 = duel(data, "Conkeldurr", {"SeismicToss"}, "Snorlax", {"Growl"});
  REQUIRE(dmgOn(engine.resolveTurn(s1, UseMove{0}, UseMove{0}, rng), 1) == 100);

  auto s2 = duel(data, "Conkeldurr", {"SeismicToss"}, "MegaGengar", {"Growl"});
  auto e2 = engine.resolveTurn(s2, UseMove{0}, UseMove{0}, rng);
  REQUIRE(s2.teams[1][0].currentHp == s2.teams[1][0].stats.hp);
  REQUIRE(countEv<MoveFailedEvent>(e2) == 0); // immune, pas "echoue" : event de degats a 0
}

TEST_CASE("Ruination removes half the CURRENT HP", "[mech]") {
  DataLoader data;
  engine::test::loadAll(data);
  BattleEngine engine(data);

  auto state = duel(data, "MegaGengar", {"Ruination"}, "Snorlax", {"Growl"});
  state.teams[1][0].currentHp = 200;
  FixedRNG rng(0.99f);
  auto events = engine.resolveTurn(state, UseMove{0}, UseMove{0}, rng);
  REQUIRE(dmgOn(events, 1) == 100);
  REQUIRE(state.teams[1][0].currentHp == 100);
}

TEST_CASE("Stat plumbing: BodyPress swings with Def, Psyshock lands on Def, FoulPlay "
          "borrows the target's Attack",
          "[mech]") {
  DataLoader data;
  engine::test::loadAll(data);
  BattleEngine engine(data);
  FixedRNG rng(0.99f);

  auto bodyPress = [&](int defStage) {
    auto s = duel(data, "Conkeldurr", {"BodyPress"}, "Snorlax", {"Growl"});
    s.teams[0][0].stat_stages[static_cast<size_t>(StatIndex::Def)] = defStage;
    FixedRNG r(0.99f);
    return dmgOn(engine.resolveTurn(s, UseMove{0}, UseMove{0}, r), 1);
  };
  REQUIRE(bodyPress(2) > bodyPress(0));

  auto psyshock = [&](StatIndex boosted) {
    auto s = duel(data, "MegaGengar", {"Psyshock"}, "Conkeldurr", {"Growl"});
    s.teams[1][0].stat_stages[static_cast<size_t>(boosted)] = 6;
    FixedRNG r(0.99f);
    return dmgOn(engine.resolveTurn(s, UseMove{0}, UseMove{0}, r), 1);
  };
  REQUIRE(psyshock(StatIndex::Def) < psyshock(StatIndex::SpD)); // only Def matters

  auto foulPlay = [&](int targetAtkStage) {
    auto s = duel(data, "MegaGengar", {"FoulPlay"}, "Conkeldurr", {"Growl"});
    s.teams[1][0].stat_stages[static_cast<size_t>(StatIndex::Atk)] = targetAtkStage;
    FixedRNG r(0.99f);
    return dmgOn(engine.resolveTurn(s, UseMove{0}, UseMove{0}, r), 1);
  };
  REQUIRE(foulPlay(6) > foulPlay(0) * 3);
}

TEST_CASE("SuckerPunch connects on attackers and whiffs on everything else", "[mech]") {
  DataLoader data;
  engine::test::loadAll(data);
  BattleEngine engine(data);
  FixedRNG rng(0.99f);

  auto sucker = [&](std::vector<std::string> foeMoves, int foeChoice) {
    BattleState state;
    state.teams[0][0] = buildCombatant(data, "MegaGengar", 100, {"SuckerPunch"});
    state.teams[1][0] = buildCombatant(data, "Conkeldurr", 100, foeMoves);
    state.teams[1][1] = buildCombatant(data, "Snorlax", 100, {"Tackle"});
    state.team_size = {1, 2};
    FixedRNG r(0.99f);
    Action foe = foeChoice < 0 ? Action{SwitchAction{1}} : Action{UseMove{foeChoice}};
    auto events = engine.resolveTurn(state, UseMove{0}, foe, r);
    return dmgOn(events, 1) > 0;
  };

  REQUIRE(sucker({"Tackle"}, 0));        // damaging move: hits
  REQUIRE_FALSE(sucker({"Growl"}, 0));   // status move: fails
  REQUIRE_FALSE(sucker({"Tackle"}, -1)); // switch: fails
}

TEST_CASE("FakeOut: turn one only, flinch included", "[mech]") {
  DataLoader data;
  engine::test::loadAll(data);
  BattleEngine engine(data);

  auto state = duel(data, "Luxray", {"FakeOut"}, "Conkeldurr", {"Tackle"});
  FixedRNG rng(0.99f);
  auto t1 = engine.resolveTurn(state, UseMove{0}, UseMove{0}, rng);
  REQUIRE(dmgOn(t1, 1) > 0);
  REQUIRE(countEv<MoveSkippedEvent>(t1) == 1); // Machamp flinched through its turn
  auto t2 = engine.resolveTurn(state, UseMove{0}, UseMove{0}, rng);
  REQUIRE(countEv<MoveFailedEvent>(t2) == 1); // the window closed
}

TEST_CASE("Wish heals at the end of the NEXT turn, half the caster's max HP", "[mech]") {
  DataLoader data;
  engine::test::loadAll(data);
  BattleEngine engine(data);

  auto state = duel(data, "Snorlax", {"Wish"}, "Conkeldurr", {"SwordsDance"});
  int max = state.teams[0][0].stats.hp;
  state.teams[0][0].currentHp = 50;
  FixedRNG rng(0.99f);

  auto t1 = engine.resolveTurn(state, UseMove{0}, UseMove{0}, rng);
  REQUIRE(state.teams[0][0].currentHp == 50); // nothing yet
  auto t2 = engine.resolveTurn(state, UseMove{0}, UseMove{0}, rng);
  REQUIRE(state.teams[0][0].currentHp == 50 + max / 2);
  // The second cast on t2 failed (a wish was pending): no new wish is up.
  REQUIRE(countEv<MoveFailedEvent>(t2) == 1);
}

TEST_CASE("DestinyBond drags the killer and refuses to chain", "[mech]") {
  DataLoader data;
  engine::test::loadAll(data);
  BattleEngine engine(data);

  auto state = duel(data, "MegaGengar", {"DestinyBond"}, "Conkeldurr", {"StoneEdge"});
  state.teams[0][0].currentHp = 1;
  FixedRNG rng(0.99f);
  auto events = engine.resolveTurn(state, UseMove{0}, UseMove{0}, rng);
  REQUIRE(state.teams[0][0].isFainted());
  REQUIRE(countEv<DestinyBondTriggeredEvent>(events) == 1);
  REQUIRE(state.teams[1][0].isFainted()); // dragged along

  auto s2 = duel(data, "MegaGengar", {"DestinyBond"}, "Conkeldurr", {"SwordsDance"});
  FixedRNG r2(0.99f);
  engine.resolveTurn(s2, UseMove{0}, UseMove{0}, r2);
  auto e2 = engine.resolveTurn(s2, UseMove{0}, UseMove{0}, r2);
  REQUIRE(countEv<MoveFailedEvent>(e2) == 1); // chained cast fails
}

TEST_CASE("BellyDrum: half the tank for +6, and the Sitrus combo", "[mech]") {
  DataLoader data;
  engine::test::loadAll(data);
  BattleEngine engine(data);

  auto state = duel(data, "Snorlax", {"BellyDrum"}, "Conkeldurr", {"SwordsDance"});
  state.teams[0][0].stats.hp = 460; // even max HP: the drum leaves exactly half
  state.teams[0][0].currentHp = 460;
  state.teams[0][0].item_id = data.findItemId("SitrusBerry");
  FixedRNG rng(0.99f);
  auto events = engine.resolveTurn(state, UseMove{0}, UseMove{0}, rng);
  REQUIRE(state.teams[0][0].stat_stages[static_cast<size_t>(StatIndex::Atk)] == 6);
  REQUIRE(countEv<ItemConsumedEvent>(events) == 1); // the berry pops at half
  REQUIRE(state.teams[0][0].currentHp == 230 + 460 / 4);

  // Below half: the drum refuses to play.
  auto s2 = duel(data, "Snorlax", {"BellyDrum"}, "Conkeldurr", {"SwordsDance"});
  s2.teams[0][0].currentHp = s2.teams[0][0].stats.hp / 3;
  FixedRNG r2(0.99f);
  auto e2 = engine.resolveTurn(s2, UseMove{0}, UseMove{0}, r2);
  REQUIRE(countEv<MoveFailedEvent>(e2) == 1);
  REQUIRE(s2.teams[0][0].stat_stages[static_cast<size_t>(StatIndex::Atk)] == 0);
}

TEST_CASE("SleepTalk swings while asleep and fails awake", "[mech]") {
  DataLoader data;
  engine::test::loadAll(data);
  BattleEngine engine(data);

  auto state = duel(data, "Snorlax", {"SleepTalk", "Tackle"}, "Conkeldurr", {"SwordsDance"});
  state.teams[0][0].status = Status::Sleep;
  state.teams[0][0].status_turns = 2;
  FixedRNG rng(0.99f); // rangeInt -> min: picks the first candidate (Tackle)
  auto events = engine.resolveTurn(state, UseMove{0}, UseMove{0}, rng);
  REQUIRE(dmgOn(events, 1) > 0); // Tackle came out of the dream
  REQUIRE(state.teams[0][0].status == Status::Sleep);

  auto s2 = duel(data, "Snorlax", {"SleepTalk", "Tackle"}, "Conkeldurr", {"SwordsDance"});
  FixedRNG r2(0.99f);
  auto e2 = engine.resolveTurn(s2, UseMove{0}, UseMove{0}, r2);
  REQUIRE(countEv<MoveFailedEvent>(e2) == 1); // wide awake: nothing to babble
}

TEST_CASE("SpectralThief pockets the boosts before hitting", "[mech]") {
  DataLoader data;
  engine::test::loadAll(data);
  BattleEngine engine(data);

  auto state = duel(data, "MegaGengar", {"SpectralThief"}, "Snorlax", {"SwordsDance"});
  state.teams[1][0].stat_stages[static_cast<size_t>(StatIndex::Atk)] = 2;
  FixedRNG rng(0.99f);
  engine.resolveTurn(state, UseMove{0}, UseMove{0}, rng);
  // Snorlax's +2 moved over BEFORE the hit; its in-turn SwordsDance (slower)
  // then rebuilt +2 on its own side.
  REQUIRE(state.teams[0][0].stat_stages[static_cast<size_t>(StatIndex::Atk)] == 2);
}

TEST_CASE("CeaselessEdge seeds a Spikes layer on every connect", "[mech]") {
  DataLoader data;
  engine::test::loadAll(data);
  BattleEngine engine(data);

  auto state = duel(data, "Corviknight", {"CeaselessEdge"}, "Snorlax", {"Growl"});
  FixedRNG rng(0.99f);
  engine.resolveTurn(state, UseMove{0}, UseMove{0}, rng);
  REQUIRE(state.hazards[1].spikes == 1);
}

TEST_CASE("BanefulBunker poisons contact attackers only", "[mech]") {
  DataLoader data;
  engine::test::loadAll(data);
  BattleEngine engine(data);

  auto poke = [&](const char *foeMove) {
    auto state = duel(data, "MegaGengar", {"BanefulBunker"}, "Conkeldurr", {foeMove});
    FixedRNG rng(0.99f);
    engine.resolveTurn(state, UseMove{0}, UseMove{0}, rng);
    return state.teams[1][0].status;
  };
  REQUIRE(poke("Tackle") == Status::Poison);  // contact: pricked
  REQUIRE(poke("StoneEdge") == Status::None); // no contact: safe
}

TEST_CASE("PhantomForce vanishes, then strikes through Protect", "[mech]") {
  DataLoader data;
  engine::test::loadAll(data);
  BattleEngine engine(data);

  auto state = duel(data, "MegaGengar", {"PhantomForce"}, "Conkeldurr", {"Tackle", "Protect"});
  FixedRNG rng(0.99f);
  auto t1 = engine.resolveTurn(state, UseMove{0}, UseMove{0}, rng);
  REQUIRE(state.teams[0][0].invulnerable_state == 3);
  REQUIRE(dmgOn(t1, 0) == -1); // the Tackle found nobody home

  auto t2 = engine.resolveTurn(state, UseMove{0}, UseMove{1}, rng);
  REQUIRE(dmgOn(t2, 1) > 0); // released through the Protect
  REQUIRE(state.teams[0][0].invulnerable_state == 0);
}

TEST_CASE("Teleport pivots out at -6 priority", "[mech]") {
  DataLoader data;
  engine::test::loadAll(data);
  BattleEngine engine(data);

  BattleState state;
  state.teams[0][0] = buildCombatant(data, "Luxray", 100, {"Teleport"});
  state.teams[0][1] = buildCombatant(data, "Snorlax", 100, {"Tackle"});
  state.teams[1][0] = buildCombatant(data, "Conkeldurr", 100, {"Tackle"});
  state.team_size = {2, 1};
  FixedRNG rng(0.99f);
  auto events = engine.resolveTurn(state, UseMove{0}, UseMove{0}, rng);

  for (const auto &ev : events)
    if (auto *e = std::get_if<MoveUsedEvent>(&ev)) {
      REQUIRE(e->user.side == 1); // -6: the fast mouse waits its turn
      break;
    }
  REQUIRE(state.activeIndex[0] == 1); // and then slips away
}

TEST_CASE("Toxic from a Poison-type never misses (and misses otherwise)", "[mech]") {
  DataLoader data;
  engine::test::loadAll(data);
  BattleEngine engine(data);
  MissRNG rng; // every accuracy roll fails

  auto s1 = duel(data, "MegaGengar", {"Toxic"}, "Snorlax", {"Growl"});
  engine.resolveTurn(s1, UseMove{0}, UseMove{0}, rng);
  REQUIRE(s1.teams[1][0].status == Status::Toxic); // Poison-type: no roll at all

  auto s2 = duel(data, "Snorlax", {"Toxic"}, "Conkeldurr", {"Growl"});
  engine.resolveTurn(s2, UseMove{0}, UseMove{0}, rng);
  REQUIRE(s2.teams[1][0].status == Status::None); // 90%: the roll happened and failed
}

TEST_CASE("Thunder ignores accuracy under the rain; Scald thaws its own user", "[mech]") {
  DataLoader data;
  engine::test::loadAll(data);
  BattleEngine engine(data);

  auto thunder = [&](bool rain) {
    auto state = duel(data, "Luxray", {"Thunder"}, "Snorlax", {"Growl"});
    if (rain) {
      state.weather = Weather::Rain;
      state.weather_turns_left = 5;
    }
    MissRNG rng;
    auto events = engine.resolveTurn(state, UseMove{0}, UseMove{0}, rng);
    return dmgOn(events, 1) > 0;
  };
  REQUIRE(thunder(true));
  REQUIRE_FALSE(thunder(false));

  auto state = duel(data, "Inteleon", {"Scald"}, "Conkeldurr", {"SwordsDance"});
  state.teams[0][0].status = Status::Freeze;
  FixedRNG rng(0.99f);
  auto events = engine.resolveTurn(state, UseMove{0}, UseMove{0}, rng);
  REQUIRE(state.teams[0][0].status == Status::None);
  REQUIRE(dmgOn(events, 1) > 0); // thawed and fired in the same breath
}

TEST_CASE("TidyUp sweeps both fields and pumps the cleaner", "[mech]") {
  DataLoader data;
  engine::test::loadAll(data);
  BattleEngine engine(data);

  auto state = duel(data, "Snorlax", {"TidyUp"}, "Conkeldurr", {"SwordsDance"});
  state.hazards[0].stealth_rock = 1;
  state.hazards[1].spikes = 2;
  FixedRNG rng(0.99f);
  engine.resolveTurn(state, UseMove{0}, UseMove{0}, rng);
  REQUIRE(state.hazards[0].stealth_rock == 0);
  REQUIRE(state.hazards[1].spikes == 0);
  REQUIRE(state.teams[0][0].stat_stages[static_cast<size_t>(StatIndex::Atk)] == 1);
  REQUIRE(state.teams[0][0].stat_stages[static_cast<size_t>(StatIndex::Spe)] == 1);
}

TEST_CASE("Defog blows the screens away along with the hazards", "[mech]") {
  DataLoader data;
  engine::test::loadAll(data);
  BattleEngine engine(data);

  auto state = duel(data, "Infernape", {"Defog"}, "Conkeldurr", {"SwordsDance"});
  state.hazards[0].spikes = 1;
  state.hazards[1].stealth_rock = 1;
  state.aurora_veil_turns[1] = 5;
  FixedRNG rng(0.99f);
  auto events = engine.resolveTurn(state, UseMove{0}, UseMove{0}, rng);
  REQUIRE(state.hazards[0].spikes == 0);
  REQUIRE(state.hazards[1].stealth_rock == 0);
  REQUIRE(state.aurora_veil_turns[1] == 0);
  REQUIRE(countEv<ScreenEndedEvent>(events) == 1);
}
