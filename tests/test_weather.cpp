#include "test_helpers.hpp"

#include "engine/battle_state.hpp"
#include "engine/data_loader.hpp"
#include "engine/engine.hpp"
#include "engine/rng.hpp"
#include "engine/validate.hpp"

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

BattleState makeDuel(const DataLoader &data, const char *s0, const char *m0, const char *s1,
                     const char *m1) {
  BattleState state;
  state.teams[0][0] = buildCombatant(data, s0, 50, {m0});
  state.teams[1][0] = buildCombatant(data, s1, 50, {m1});
  state.team_size = {1, 1};
  return state;
}

} // namespace

TEST_CASE("SetWeather starts the weather for 5 turns; same weather fails", "[weather]") {
  DataLoader data;
  data.loadAll(BATTLE_ENGINE_DATA_DIR);
  BattleEngine engine(data);

  auto state = makeDuel(data, "politoed", "RainDance", "snorlax", "Tackle");
  state.teams[0][0] = buildCombatant(data, "snorlax", 50, {"RainDance"}); // no Drizzle side effect
  FixedRNG rng(0.5f);

  auto t1 = engine.resolveTurn(state, UseMove{0}, UseMove{0}, rng);
  REQUIRE(countEvents<WeatherStartedEvent>(t1) == 1);
  REQUIRE(state.weather == Weather::Rain);
  REQUIRE(state.weather_turns_left == 4); // 5 set, minus the end of this turn

  auto t2 = engine.resolveTurn(state, UseMove{0}, UseMove{0}, rng);
  REQUIRE(countEvents<MoveFailedEvent>(t2) == 1); // rain already up
}

TEST_CASE("Weather subsides at the end of its fifth turn", "[weather]") {
  DataLoader data;
  data.loadAll(BATTLE_ENGINE_DATA_DIR);
  BattleEngine engine(data);

  auto state = makeDuel(data, "snorlax", "RainDance", "snorlax", "Tackle");
  FixedRNG rng(0.5f);

  engine.resolveTurn(state, UseMove{0}, UseMove{0}, rng); // turn 1: set
  for (int i = 0; i < 3; ++i)
    engine.resolveTurn(state, UseMove{0}, UseMove{0}, rng); // re-sets fail, rain ticks
  REQUIRE(state.weather == Weather::Rain);
  REQUIRE(state.weather_turns_left == 1);

  auto last = engine.resolveTurn(state, UseMove{0}, UseMove{0}, rng);
  // The re-set fails first (rain still up mid-turn), then rain ends.
  REQUIRE(countEvents<WeatherEndedEvent>(last) == 1);
  REQUIRE(state.weather == Weather::None);
  REQUIRE(state.weather_turns_left == 0);
}

TEST_CASE("Rain boosts Water x1.5 and halves Fire; sun mirrors it", "[weather][damage]") {
  DataLoader data;
  data.loadAll(BATTLE_ENGINE_DATA_DIR);
  BattleEngine engine(data);

  auto hit = [&](Weather w, const char *attacker, const char *move) {
    auto state = makeDuel(data, attacker, move, "snorlax", "Tackle");
    state.weather = w;
    state.weather_turns_left = (w == Weather::None) ? 0 : 5;
    FixedRNG rng(0.5f);
    auto events = engine.resolveTurn(state, UseMove{0}, UseMove{0}, rng);
    return damageOn(events, 1);
  };

  int surfDry = hit(Weather::None, "blastoise", "Surf");
  int surfRain = hit(Weather::Rain, "blastoise", "Surf");
  int surfSun = hit(Weather::Sun, "blastoise", "Surf");
  REQUIRE(surfRain > surfDry);
  REQUIRE(surfRain >= surfDry * 3 / 2 - 2);
  REQUIRE(surfSun < surfDry);

  int fireDry = hit(Weather::None, "charizard", "Flamethrower");
  int fireSun = hit(Weather::Sun, "charizard", "Flamethrower");
  int fireRain = hit(Weather::Rain, "charizard", "Flamethrower");
  REQUIRE(fireSun > fireDry);
  REQUIRE(fireRain < fireDry);
}

TEST_CASE("Sand chips 1/16 on non Rock/Ground/Steel only", "[weather][residual]") {
  DataLoader data;
  data.loadAll(BATTLE_ENGINE_DATA_DIR);
  BattleEngine engine(data);

  // Pikachu (Electric) chips; Tyranitar (Rock/Dark) is immune.
  auto state = makeDuel(data, "pikachu", "Growl", "tyranitar", "Sandstorm");
  state.weather = Weather::Sand;
  state.weather_turns_left = 5;
  int pikaHp = state.teams[0][0].currentHp;
  int ttarHp = state.teams[1][0].currentHp;

  FixedRNG rng(0.5f);
  auto events = engine.resolveTurn(state, UseMove{0}, UseMove{0}, rng);
  (void)events;

  // Sandstorm move fails (already up) but the chip applies at end of turn.
  REQUIRE(state.teams[0][0].currentHp == pikaHp - std::max(1, state.teams[0][0].stats.hp / 16));
  REQUIRE(state.teams[1][0].currentHp == ttarHp);
}

TEST_CASE("Garchomp (Ground) and Scizor (Steel) shrug off sand; hail spares no one here",
          "[weather][residual]") {
  DataLoader data;
  data.loadAll(BATTLE_ENGINE_DATA_DIR);
  BattleEngine engine(data);

  auto chip = [&](const char *species, Weather w) {
    auto state = makeDuel(data, species, "Growl", "snorlax", "Growl");
    state.weather = w;
    state.weather_turns_left = 5;
    int before = state.teams[0][0].currentHp;
    FixedRNG rng(0.5f);
    engine.resolveTurn(state, UseMove{0}, UseMove{0}, rng);
    return before - state.teams[0][0].currentHp;
  };

  REQUIRE(chip("garchomp", Weather::Sand) == 0);
  REQUIRE(chip("scizor", Weather::Sand) == 0);
  REQUIRE(chip("gengar", Weather::Sand) > 0);   // Levitate does NOT protect
  REQUIRE(chip("garchomp", Weather::Hail) > 0); // no Ice types in the roster
}

TEST_CASE("Weather chip resolves before status residuals", "[weather][residual]") {
  DataLoader data;
  data.loadAll(BATTLE_ENGINE_DATA_DIR);
  BattleEngine engine(data);

  auto state = makeDuel(data, "pikachu", "Growl", "snorlax", "Growl");
  state.weather = Weather::Sand;
  state.weather_turns_left = 5;
  state.teams[0][0].status = Status::Burn;

  FixedRNG rng(0.5f);
  auto events = engine.resolveTurn(state, UseMove{0}, UseMove{0}, rng);

  int weatherIdx = -1, statusIdx = -1;
  for (int i = 0; i < static_cast<int>(events.size()); ++i) {
    if (std::holds_alternative<WeatherDamageEvent>(events[static_cast<size_t>(i)]))
      weatherIdx = i;
    if (std::holds_alternative<StatusDamageEvent>(events[static_cast<size_t>(i)]) && statusIdx < 0)
      statusIdx = i;
  }
  REQUIRE(weatherIdx >= 0);
  REQUIRE(statusIdx >= 0);
  REQUIRE(weatherIdx < statusIdx);
}

TEST_CASE("Sandstorm gives Rock types SpD x1.5 against special moves", "[weather][damage]") {
  DataLoader data;
  data.loadAll(BATTLE_ENGINE_DATA_DIR);
  BattleEngine engine(data);

  auto hit = [&](Weather w, const char *move) {
    auto state = makeDuel(data, "blastoise", move, "tyranitar", "Growl");
    state.weather = w;
    state.weather_turns_left = (w == Weather::None) ? 0 : 5;
    // neutralize Drizzle/SandStream interference: weather forced by hand
    FixedRNG rng(0.5f);
    auto events = engine.resolveTurn(state, UseMove{0}, UseMove{0}, rng);
    return damageOn(events, 1);
  };

  int surfDry = hit(Weather::None, "Surf");
  int surfSand = hit(Weather::Sand, "Surf");
  REQUIRE(surfSand < surfDry); // special: boosted SpD

  int slamDry = hit(Weather::None, "BodySlam");
  int slamSand = hit(Weather::Sand, "BodySlam");
  REQUIRE(slamSand == slamDry); // physical: untouched
}

TEST_CASE("SandStream and Drizzle set their weather on switch-in", "[weather][ability]") {
  DataLoader data;
  data.loadAll(BATTLE_ENGINE_DATA_DIR);
  BattleEngine engine(data);

  BattleState state;
  state.teams[0][0] = buildCombatant(data, "snorlax", 50, {"Tackle"});
  state.teams[0][1] = buildCombatant(data, "tyranitar", 50, {"StoneEdge"});
  state.teams[1][0] = buildCombatant(data, "machamp", 50, {"CloseCombat"});
  state.team_size = {2, 1};

  FixedRNG rng(0.5f);
  auto events = engine.resolveTurn(state, SwitchAction{1}, UseMove{0}, rng);

  REQUIRE(countEvents<WeatherStartedEvent>(events) == 1);
  REQUIRE(state.weather == Weather::Sand);
}

TEST_CASE("startBattle: the slower weather ability wins the war", "[weather][ability]") {
  DataLoader data;
  data.loadAll(BATTLE_ENGINE_DATA_DIR);
  BattleEngine engine(data);

  BattleState state;
  state.teams[0][0] = buildCombatant(data, "tyranitar", 50, {"StoneEdge"}); // 66 speed
  state.teams[1][0] = buildCombatant(data, "politoed", 50, {"Surf"});       // 75 speed
  state.team_size = {1, 1};

  FixedRNG srng(0.5f);
  auto events = engine.startBattle(state, srng);

  // Politoed (faster) fires Drizzle first, Tyranitar overrides: sand stays.
  REQUIRE(countEvents<WeatherStartedEvent>(events) == 2);
  REQUIRE(state.weather == Weather::Sand);
}

TEST_CASE("Weather ability is silent if its weather is already up", "[weather][ability]") {
  DataLoader data;
  data.loadAll(BATTLE_ENGINE_DATA_DIR);
  BattleEngine engine(data);

  BattleState state;
  state.teams[0][0] = buildCombatant(data, "snorlax", 50, {"Tackle"});
  state.teams[0][1] = buildCombatant(data, "politoed", 50, {"Surf"});
  state.teams[1][0] = buildCombatant(data, "machamp", 50, {"CloseCombat"});
  state.team_size = {2, 1};
  state.weather = Weather::Rain;
  state.weather_turns_left = 5;

  FixedRNG rng(0.5f);
  auto events = engine.resolveTurn(state, SwitchAction{1}, UseMove{0}, rng);

  REQUIRE(countEvents<WeatherStartedEvent>(events) == 0);
  REQUIRE(state.weather_turns_left == 4); // untouched by the ability, ticked by end of turn
}

TEST_CASE("validateState checks weather fields", "[weather][validate]") {
  DataLoader data;
  data.loadAll(BATTLE_ENGINE_DATA_DIR);

  auto state = makeDuel(data, "snorlax", "Tackle", "machamp", "CloseCombat");
  REQUIRE_NOTHROW(validateState(state, data));

  SECTION("turns without weather") {
    state.weather_turns_left = 3;
    REQUIRE_THROWS_AS(validateState(state, data), std::invalid_argument);
  }
  SECTION("negative turns") {
    state.weather = Weather::Rain;
    state.weather_turns_left = -1;
    REQUIRE_THROWS_AS(validateState(state, data), std::invalid_argument);
  }
  SECTION("turns above the max duration") {
    state.weather = Weather::Sand;
    state.weather_turns_left = 9;
    REQUIRE_THROWS_AS(validateState(state, data), std::invalid_argument);
  }
  SECTION("valid active weather") {
    state.weather = Weather::Hail;
    state.weather_turns_left = 5;
    REQUIRE_NOTHROW(validateState(state, data));
  }
}
