#include "helpers.hpp"

#include "engine/core/battle_state.hpp"
#include "engine/core/data_loader.hpp"
#include "engine/core/engine.hpp"
#include "engine/core/rng.hpp"
#include "engine/core/validate.hpp"
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

// chancePct always fails (accuracy rolls miss), chance() never procs.
class MissRNG : public RNG {
public:
  int rangeInt(int /*min*/, int max) override { return max; }
  float unit() override { return 0.99f; }
};

BattleState makeDuel(const DataLoader &data, const char *s0, std::vector<std::string> m0,
                     const char *s1, std::vector<std::string> m1) {
  BattleState state;
  state.teams[0][0] = buildCombatant(data, s0, m0);
  state.teams[1][0] = buildCombatant(data, s1, m1);
  state.team_size = {1, 1};
  return state;
}

void setSnow(BattleState &state) {
  state.weather = Weather::Snow;
  state.weather_turns_left = 5;
}

void setElectricTerrain(BattleState &state) {
  state.terrain = Terrain::Electric;
  state.terrain_turns_left = 5;
}

} // namespace

TEST_CASE("Snow: Ice types get Def x1.5, special side untouched (ADR #37)", "[field][snow]") {
  DataLoader data;
  engine::test::loadAll(data);
  BattleEngine engine(data);

  auto hit = [&](const char *move, bool snow) {
    auto state = makeDuel(data, "Conkeldurr", {move}, "Mamoswine", {"Tackle"});
    if (snow)
      setSnow(state);
    FixedRNG rng(0.99f);
    auto events = engine.resolveTurn(state, UseMove{0}, UseMove{0}, rng);
    return damageOn(events, 1);
  };

  int physClear = hit("Tackle", false);
  int physSnow = hit("Tackle", true);
  REQUIRE(physSnow < physClear);
  REQUIRE(physSnow >= static_cast<int>(static_cast<float>(physClear) / 1.6f));

  // Flamethrower is special: snow leaves it alone.
  REQUIRE(hit("Flamethrower", true) == hit("Flamethrower", false));
}

TEST_CASE("The Hail fixture move now sets snow; snow never chips", "[field][snow]") {
  DataLoader data;
  engine::test::loadAll(data);
  BattleEngine engine(data);

  auto state = makeDuel(data, "Luxray", {"Hail"}, "Snorlax", {"Growl"});
  FixedRNG rng(0.99f);
  auto events = engine.resolveTurn(state, UseMove{0}, UseMove{0}, rng);
  REQUIRE(state.weather == Weather::Snow);
  REQUIRE(countEvents<WeatherDamageEvent>(events) == 0);
  REQUIRE(state.teams[0][0].currentHp == state.teams[0][0].stats.hp);
}

TEST_CASE("Blizzard never misses under snow (accuracyInWeather)", "[field][snow]") {
  DataLoader data;
  engine::test::loadAll(data);
  BattleEngine engine(data);

  auto run = [&](bool snow) {
    auto state = makeDuel(data, "Mamoswine", {"Blizzard"}, "Snorlax", {"Growl"});
    if (snow)
      setSnow(state);
    MissRNG rng; // every accuracy roll fails: only a never-miss connects
    auto events = engine.resolveTurn(state, UseMove{0}, UseMove{0}, rng);
    return countEvents<MissedEvent>(events);
  };

  REQUIRE(run(false) == 1); // 70 accuracy: the roll happens and fails
  REQUIRE(run(true) == 0);  // snow: the roll is skipped entirely
}

TEST_CASE("Electric Terrain: x1.3 for grounded attackers only (ADR #38)", "[field][terrain]") {
  DataLoader data;
  engine::test::loadAll(data);
  BattleEngine engine(data);

  auto bolt = [&](const char *attacker, bool terrain) {
    auto state = makeDuel(data, attacker, {"Thunderbolt"}, "Snorlax", {"Growl"});
    if (terrain)
      setElectricTerrain(state);
    FixedRNG rng(0.99f);
    auto events = engine.resolveTurn(state, UseMove{0}, UseMove{0}, rng);
    return damageOn(events, 1);
  };

  int plain = bolt("Luxray", false);
  int boosted = bolt("Luxray", true);
  REQUIRE(boosted > static_cast<int>(static_cast<float>(plain) * 1.2f));

  // Gengar levitates: the terrain never reaches it.
  REQUIRE(bolt("MegaGengar", true) == bolt("MegaGengar", false));
}

TEST_CASE("Electric Terrain keeps grounded Pokemon awake, Levitate exempt", "[field][terrain]") {
  DataLoader data;
  engine::test::loadAll(data);
  BattleEngine engine(data);

  auto sporeOn = [&](const char *target) {
    auto state = makeDuel(data, "Toxapex", {"Spore"}, target, {"Growl"});
    setElectricTerrain(state);
    FixedRNG rng(0.99f);
    engine.resolveTurn(state, UseMove{0}, UseMove{0}, rng);
    return state.teams[1][0].status;
  };

  REQUIRE(sporeOn("Snorlax") == Status::None);     // grounded: protected
  REQUIRE(sporeOn("MegaGengar") == Status::Sleep); // Levitate: fair game
}

TEST_CASE("Rest fails for a grounded user under Electric Terrain (canon)", "[field][terrain]") {
  DataLoader data;
  engine::test::loadAll(data);
  BattleEngine engine(data);

  auto rest = [&](const char *user) {
    auto state = makeDuel(data, user, {"Rest"}, "Conkeldurr", {"Growl"});
    setElectricTerrain(state);
    state.teams[0][0].currentHp = state.teams[0][0].stats.hp / 2;
    FixedRNG rng(0.99f);
    auto events = engine.resolveTurn(state, UseMove{0}, UseMove{0}, rng);
    return std::pair{state.teams[0][0].status, countEvents<MoveFailedEvent>(events)};
  };

  auto [laxStatus, laxFails] = rest("Snorlax");
  REQUIRE(laxStatus == Status::None);
  REQUIRE(laxFails == 1); // no sleep, no heal: the whole move fails

  auto [gengarStatus, gengarFails] = rest("MegaGengar");
  REQUIRE(gengarStatus == Status::Sleep);
  REQUIRE(gengarFails == 0);
}

TEST_CASE("The terrain ticks like the weather and ends after 5 turns", "[field][terrain]") {
  DataLoader data;
  engine::test::loadAll(data);
  BattleEngine engine(data);

  auto state = makeDuel(data, "Snorlax", {"Growl"}, "Conkeldurr", {"SwordsDance"});
  setElectricTerrain(state);
  FixedRNG rng(0.99f);

  for (int i = 0; i < 4; ++i)
    engine.resolveTurn(state, UseMove{0}, UseMove{0}, rng);
  REQUIRE(state.terrain == Terrain::Electric);
  REQUIRE(state.terrain_turns_left == 1);

  auto events = engine.resolveTurn(state, UseMove{0}, UseMove{0}, rng);
  REQUIRE(countEvents<TerrainEndedEvent>(events) == 1);
  REQUIRE(state.terrain == Terrain::None);
  REQUIRE(state.terrain_turns_left == 0);
}

TEST_CASE("Voile Aurore needs snow and refuses to stack (ADR #39)", "[field][screen]") {
  DataLoader data;
  engine::test::loadAll(data);
  BattleEngine engine(data);

  auto state = makeDuel(data, "Mamoswine", {"AuroraVeil"}, "Conkeldurr", {"SwordsDance"});
  FixedRNG rng(0.99f);

  // No snow: the screen refuses to go up.
  auto e1 = engine.resolveTurn(state, UseMove{0}, UseMove{0}, rng);
  REQUIRE(countEvents<MoveFailedEvent>(e1) == 1);
  REQUIRE(state.aurora_veil_turns[0] == 0);

  setSnow(state);
  auto e2 = engine.resolveTurn(state, UseMove{0}, UseMove{0}, rng);
  REQUIRE(countEvents<ScreenStartedEvent>(e2) == 1);
  REQUIRE(state.aurora_veil_turns[0] == 4); // 5 turns, set turn included

  // Already up: a second cast fails.
  auto e3 = engine.resolveTurn(state, UseMove{0}, UseMove{0}, rng);
  REQUIRE(countEvents<MoveFailedEvent>(e3) == 1);
}

TEST_CASE("Voile Aurore halves both categories; crits punch through", "[field][screen]") {
  DataLoader data;
  engine::test::loadAll(data);
  BattleEngine engine(data);

  auto hit = [&](const char *move, bool veil, float rngUnit) {
    auto state = makeDuel(data, "Conkeldurr", {move}, "Snorlax", {"Growl"});
    setSnow(state);
    if (veil)
      state.aurora_veil_turns[1] = 5;
    FixedRNG rng(rngUnit);
    auto events = engine.resolveTurn(state, UseMove{0}, UseMove{0}, rng);
    return damageOn(events, 1);
  };

  int phys = hit("Tackle", false, 0.99f);
  int physVeil = hit("Tackle", true, 0.99f);
  REQUIRE(physVeil <= phys / 2 + 1);
  REQUIRE(physVeil >= static_cast<int>(static_cast<float>(phys) * 0.45f));

  int spec = hit("Flamethrower", false, 0.99f);
  int specVeil = hit("Flamethrower", true, 0.99f);
  REQUIRE(specVeil <= spec / 2 + 1);

  // FixedRNG(0.0) forces the crit: the screen is ignored (and so are
  // Growl's Atk drops, but both runs share that).
  int crit = hit("Tackle", false, 0.0f);
  int critVeil = hit("Tackle", true, 0.0f);
  REQUIRE(critVeil == crit);
}

TEST_CASE("Voile Aurore lasts 8 turns when the setter holds LightClay", "[field][screen]") {
  DataLoader data;
  engine::test::loadAll(data);
  BattleEngine engine(data);

  auto state = makeDuel(data, "Mamoswine", {"AuroraVeil"}, "Conkeldurr", {"SwordsDance"});
  setSnow(state);
  state.teams[0][0].item_id = data.findItemId("LightClay");
  FixedRNG rng(0.99f);

  auto events = engine.resolveTurn(state, UseMove{0}, UseMove{0}, rng);
  for (const auto &ev : events)
    if (auto *e = std::get_if<ScreenStartedEvent>(&ev))
      REQUIRE(e->turns == 8);
  REQUIRE(state.aurora_veil_turns[0] == 7);
}

TEST_CASE("The screen counts down and ends with its event", "[field][screen]") {
  DataLoader data;
  engine::test::loadAll(data);
  BattleEngine engine(data);

  auto state = makeDuel(data, "Snorlax", {"Growl"}, "Conkeldurr", {"SwordsDance"});
  state.aurora_veil_turns[0] = 2;
  FixedRNG rng(0.99f);

  engine.resolveTurn(state, UseMove{0}, UseMove{0}, rng);
  REQUIRE(state.aurora_veil_turns[0] == 1);
  auto events = engine.resolveTurn(state, UseMove{0}, UseMove{0}, rng);
  REQUIRE(countEvents<ScreenEndedEvent>(events) == 1);
  REQUIRE(state.aurora_veil_turns[0] == 0);
}

TEST_CASE("validateState checks the terrain and screen invariants", "[field][validate]") {
  DataLoader data;
  engine::test::loadAll(data);

  auto state = makeDuel(data, "Snorlax", {"Tackle"}, "Conkeldurr", {"Growl"});
  setElectricTerrain(state);
  state.aurora_veil_turns[1] = 8;
  REQUIRE_NOTHROW(validateState(state, data));

  state.terrain_turns_left = 9;
  REQUIRE_THROWS_AS(validateState(state, data), std::invalid_argument);
  state.terrain = Terrain::None;
  state.terrain_turns_left = 3; // turns without a terrain
  REQUIRE_THROWS_AS(validateState(state, data), std::invalid_argument);
  state.terrain_turns_left = 0;

  state.aurora_veil_turns[1] = 9;
  REQUIRE_THROWS_AS(validateState(state, data), std::invalid_argument);
}
