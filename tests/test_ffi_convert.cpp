#include "engine/ffi/convert.hpp"
#include "engine/ffi/ffi.hpp"

#include <catch2/catch_test_macros.hpp>

#include <stdexcept>
#include <string>

using namespace engine;
using namespace engine::ffi;

namespace {
void ensureInit() { engine_init(BATTLE_ENGINE_DATA_DIR); }

FfiEvent one(const BattleEvent &ev) {
  EventLog log{ev};
  std::vector<FfiEvent> out = flatten(log);
  REQUIRE(out.size() == 1);
  return out[0];
}

const CombatantRef kA{0, 2};
const CombatantRef kB{1, 0};

template <typename F> std::string messageOf(F &&f) {
  try {
    f();
  } catch (const std::exception &e) {
    return e.what();
  }
  return {};
}

bool startsWith(const std::string &s, const char *prefix) { return s.rfind(prefix, 0) == 0; }
} // namespace

TEST_CASE("toAction rebuilds the variant from the tag", "[ffi][convert]") {
  SECTION("a move keeps its slot and pivot target") {
    Action a = toAction(FfiAction{0, 3, 5});
    const auto *m = std::get_if<UseMove>(&a);
    REQUIRE(m != nullptr);
    REQUIRE(m->moveIndex == 3);
    REQUIRE(m->pivotTarget == 5);
  }

  SECTION("-1 stays -1: PivotEffect re-validates and falls back to auto") {
    Action a = toAction(FfiAction{0, 0, -1});
    REQUIRE(std::get<UseMove>(a).pivotTarget == -1);
  }

  SECTION("a switch keeps its team index") {
    Action a = toAction(FfiAction{1, 4, -1});
    const auto *s = std::get_if<SwitchAction>(&a);
    REQUIRE(s != nullptr);
    REQUIRE(s->teamIndex == 4);
  }

  SECTION("an unknown kind is the one failure a variant cannot have") {
    const std::string msg = messageOf([] { return toAction(FfiAction{7, 0, -1}); });
    INFO(msg);
    REQUIRE(startsWith(msg, "E_ACTION:"));
  }
}

TEST_CASE("flatten maps the combatant reference", "[ffi][convert]") {
  ensureInit();

  SECTION("side and slot come from the event's own reference") {
    FfiEvent f = one(FaintedEvent{kA});
    REQUIRE(f.kind == 2);
    REQUIRE(f.side == 0);
    REQUIRE(f.slot == 2);
    REQUIRE(f.name_id == kFfiNoName);
  }

  SECTION("field-wide events leave both at -1") {
    FfiEvent f = one(WeatherStartedEvent{Weather::Sand});
    REQUIRE(f.side == -1);
    REQUIRE(f.slot == -1);
  }

  SECTION("side-scoped events carry a side but no slot") {
    FfiEvent f = one(HazardsClearedEvent{1});
    REQUIRE(f.side == 1);
    REQUIRE(f.slot == -1);
  }

  SECTION("an empty log flattens to an empty vector") {
    EventLog log;
    REQUIRE(flatten(log).empty());
  }

  SECTION("order is preserved") {
    EventLog log{MoveUsedEvent{kA, "ShadowBall"}, DamageDealtEvent{kB, 87, 2.0f, true, false},
                 FaintedEvent{kB}};
    std::vector<FfiEvent> out = flatten(log);
    REQUIRE(out.size() == 3);
    REQUIRE(out[0].kind == 0);
    REQUIRE(out[1].kind == 1);
    REQUIRE(out[2].kind == 2);
  }
}

TEST_CASE("every kind matches the contract table", "[ffi][convert]") {
  ensureInit();
  const int shadowBall = find_move_id("ShadowBall");
  const int lifeOrb = find_item_id("LifeOrb");
  const int blaze = find_ability_id("Blaze");
  REQUIRE(shadowBall >= 0);
  REQUIRE(lifeOrb >= 0);
  REQUIRE(blaze >= 0);

  SECTION("0 MoveUsed") {
    FfiEvent f = one(MoveUsedEvent{kA, "ShadowBall"});
    REQUIRE(f.kind == 0);
    REQUIRE(f.name_id == shadowBall);
  }

  SECTION("1 DamageDealt carries effectiveness in f0 and the two flags") {
    FfiEvent f = one(DamageDealtEvent{kB, 87, 2.0f, true, true});
    REQUIRE(f.kind == 1);
    REQUIRE(f.i0 == 87);
    REQUIRE(f.f0 == 2.0f);
    REQUIRE((f.flags & kFfiFlagStab) != 0);
    REQUIRE((f.flags & kFfiFlagCrit) != 0);

    FfiEvent plain = one(DamageDealtEvent{kB, 10, 0.5f, false, false});
    REQUIRE(plain.flags == 0);
    REQUIRE(plain.f0 == 0.5f);
  }

  SECTION("3 Missed") {
    FfiEvent f = one(MissedEvent{kA, "ShadowBall"});
    REQUIRE(f.kind == 3);
    REQUIRE(f.name_id == shadowBall);
  }

  SECTION("4/5/7 status in i0") {
    REQUIRE(one(StatusAppliedEvent{kB, Status::Burn}).i0 == static_cast<int>(Status::Burn));
    REQUIRE(one(StatusFailedEvent{kB, Status::Sleep}).kind == 5);
    REQUIRE(one(StatusCuredEvent{kB, Status::Toxic}).kind == 7);
  }

  SECTION("6 StatusDamage: damage in i0, status in i1") {
    FfiEvent f = one(StatusDamageEvent{kB, Status::Toxic, 17});
    REQUIRE(f.kind == 6);
    REQUIRE(f.i0 == 17);
    REQUIRE(f.i1 == static_cast<int>(Status::Toxic));
  }

  SECTION("8 MoveSkipped") {
    FfiEvent f = one(MoveSkippedEvent{kA, SkipReason::FullyParalyzed});
    REQUIRE(f.kind == 8);
    REQUIRE(f.i0 == static_cast<int>(SkipReason::FullyParalyzed));
  }

  SECTION("9 StatStageChanged: the DELTA in i0, not the resulting stage") {
    FfiEvent f = one(StatStageChangedEvent{kA, StatIndex::Spe, -2});
    REQUIRE(f.kind == 9);
    REQUIRE(f.i0 == -2);
    REQUIRE(f.i1 == static_cast<int>(StatIndex::Spe));
  }

  SECTION("10 StatChangeFailed: stat in i0, direction in i1") {
    FfiEvent f = one(StatChangeFailedEvent{kA, StatIndex::Atk, true});
    REQUIRE(f.kind == 10);
    REQUIRE(f.i0 == static_cast<int>(StatIndex::Atk));
    REQUIRE(f.i1 == 1);
    REQUIRE(one(StatChangeFailedEvent{kA, StatIndex::Atk, false}).i1 == 0);
  }

  SECTION("11/12 switches") {
    REQUIRE(one(SwitchedOutEvent{kA}).kind == 11);
    REQUIRE(one(SwitchedInEvent{kA}).kind == 12);
  }

  SECTION("13 AbilityTriggered resolves against the ability catalog") {
    FfiEvent f = one(AbilityTriggeredEvent{kA, "Blaze"});
    REQUIRE(f.kind == 13);
    REQUIRE(f.name_id == blaze);
  }

  SECTION("14 MoveFailed") {
    REQUIRE(one(MoveFailedEvent{kA, "ShadowBall"}).kind == 14);
  }

  SECTION("15/16/17 weather") {
    REQUIRE(one(WeatherStartedEvent{Weather::Rain}).i0 == static_cast<int>(Weather::Rain));
    REQUIRE(one(WeatherEndedEvent{Weather::Rain}).kind == 16);
    FfiEvent f = one(WeatherDamageEvent{kB, Weather::Sand, 12});
    REQUIRE(f.kind == 17);
    REQUIRE(f.i0 == 12);
    REQUIRE(f.i1 == static_cast<int>(Weather::Sand));
  }

  SECTION("18 HazardSet: side is the one that RECEIVES, layers in i1") {
    FfiEvent f = one(HazardSetEvent{1, HazardKind::Spikes, 3});
    REQUIRE(f.kind == 18);
    REQUIRE(f.side == 1);
    REQUIRE(f.slot == -1);
    REQUIRE(f.i0 == static_cast<int>(HazardKind::Spikes));
    REQUIRE(f.i1 == 3);
  }

  SECTION("19/20/21 hazards") {
    FfiEvent f = one(HazardDamageEvent{kB, HazardKind::StealthRock, 25});
    REQUIRE(f.kind == 19);
    REQUIRE(f.i0 == 25);
    REQUIRE(f.i1 == static_cast<int>(HazardKind::StealthRock));
    REQUIRE(one(HazardsClearedEvent{0}).kind == 20);
    REQUIRE(one(ToxicSpikesAbsorbedEvent{kB}).kind == 21);
  }

  SECTION("22/23 heal and recoil") {
    REQUIRE(one(HealedEvent{kA, 44}).i0 == 44);
    FfiEvent r = one(RecoilDamageEvent{kA, 19});
    REQUIRE(r.kind == 23);
    REQUIRE(r.i0 == 19);
  }

  SECTION("24 Charging / 25 Protected") {
    FfiEvent c = one(ChargingEvent{kA, "PhantomForce"});
    REQUIRE(c.kind == 24);
    REQUIRE(c.name_id == find_move_id("PhantomForce"));
    REQUIRE(one(ProtectedEvent{kB}).kind == 25);
  }

  SECTION("26-29 items resolve against the item catalog") {
    REQUIRE(one(ItemTriggeredEvent{kA, "LifeOrb"}).name_id == lifeOrb);
    REQUIRE(one(ItemConsumedEvent{kA, "SitrusBerry"}).kind == 27);
    FfiEvent d = one(ItemDamageEvent{kA, "LifeOrb", 32});
    REQUIRE(d.kind == 28);
    REQUIRE(d.name_id == lifeOrb);
    REQUIRE(d.i0 == 32);
    REQUIRE(one(ItemKnockedOffEvent{kA, "Leftovers"}).kind == 29);
  }

  SECTION("30/31 terrain, 32/33 screens") {
    REQUIRE(one(TerrainStartedEvent{Terrain::Electric}).i0 == static_cast<int>(Terrain::Electric));
    REQUIRE(one(TerrainEndedEvent{Terrain::Electric}).kind == 31);
    FfiEvent s = one(ScreenStartedEvent{0, 5});
    REQUIRE(s.kind == 32);
    REQUIRE(s.side == 0);
    REQUIRE(s.i0 == 5);
    REQUIRE(one(ScreenEndedEvent{0}).kind == 33);
  }

  SECTION("34 DestinyBond names the attacker taken along") {
    FfiEvent f = one(DestinyBondTriggeredEvent{kB});
    REQUIRE(f.kind == 34);
    REQUIRE(f.side == 1);
    REQUIRE(f.slot == 0);
  }

  SECTION("35 AbilityDamage sits apart from ItemDamage") {
    FfiEvent f = one(AbilityDamageEvent{kA, "Blaze", 13});
    REQUIRE(f.kind == 35);
    REQUIRE(f.name_id == blaze);
    REQUIRE(f.i0 == 13);
    // The point of the flat table: 28 and 35 resolve against different
    // catalogs, so they cannot share a kind.
    REQUIRE(one(ItemDamageEvent{kA, "LifeOrb", 13}).kind != f.kind);
  }
}

TEST_CASE("Struggle flattens to its sentinel, not to an error", "[ffi][convert]") {
  ensureInit();
  REQUIRE(one(MoveUsedEvent{kA, "Struggle"}).name_id == kFfiStruggle);
  REQUIRE(one(MissedEvent{kA, "Struggle"}).name_id == kFfiStruggle);
  REQUIRE(one(AbilityTriggeredEvent{kA, "StruggleRecoil"}).name_id == kFfiStruggleRecoil);
}

TEST_CASE("a name absent from the catalog is an internal inconsistency", "[ffi][convert]") {
  ensureInit();
  // Not user input: the event stream and the catalog disagree. Degrading to a
  // silent -1 would surface months later as an empty label in the frontend.
  for (const std::string &msg : {
           messageOf([] { return one(MoveUsedEvent{kA, "NoSuchMove"}); }),
           messageOf([] { return one(ItemTriggeredEvent{kA, "NoSuchItem"}); }),
           messageOf([] { return one(AbilityTriggeredEvent{kA, "NoSuchAbility"}); }),
       }) {
    INFO(msg);
    REQUIRE(startsWith(msg, "E_ARG:"));
  }
}
