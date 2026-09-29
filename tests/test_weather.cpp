#include "helpers.hpp"

#include "engine/core/battle_state.hpp"
#include "engine/core/data_loader.hpp"
#include "engine/core/engine.hpp"
#include "engine/core/rng.hpp"
#include "engine/core/validate.hpp"

#include <catch2/catch_test_macros.hpp>

#include <stdexcept>
#include <variant>

using namespace engine;
using engine::test::buildCombatant;
using engine::test::overrideAbility;

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
  state.teams[0][0] = buildCombatant(data, s0, {m0});
  state.teams[1][0] = buildCombatant(data, s1, {m1});
  state.team_size = {1, 1};
  return state;
}
} // namespace

TEST_CASE("SetWeather starts the weather for 5 turns; same weather fails", "[weather]") {
  DataLoader data;
  engine::test::loadAll(data);
  BattleEngine engine(data);

  auto state = makeDuel(data, "Quagsire", "RainDance", "Snorlax", "Tackle");
  state.teams[0][0] = buildCombatant(data, "Snorlax", {"RainDance"});
  FixedRNG rng(0.5f);

  auto t1 = engine.resolveTurn(state, UseMove{0}, UseMove{0}, rng);
  REQUIRE(countEvents<WeatherStartedEvent>(t1) == 1);
  REQUIRE(state.weather == Weather::Rain);
  REQUIRE(state.weather_turns_left == 4);

  auto t2 = engine.resolveTurn(state, UseMove{0}, UseMove{0}, rng);
  REQUIRE(countEvents<MoveFailedEvent>(t2) == 1);
}

TEST_CASE("Weather subsides at the end of its fifth turn", "[weather]") {
  DataLoader data;
  engine::test::loadAll(data);
  BattleEngine engine(data);

  auto state = makeDuel(data, "Snorlax", "RainDance", "Snorlax", "Tackle");
  FixedRNG rng(0.5f);

  engine.resolveTurn(state, UseMove{0}, UseMove{0}, rng);
  for (int i = 0; i < 3; ++i)
    engine.resolveTurn(state, UseMove{0}, UseMove{0}, rng);
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
  engine::test::loadAll(data);
  BattleEngine engine(data);

  auto hit = [&](Weather w, const char *attacker, const char *move) {
    auto state = makeDuel(data, attacker, move, "Snorlax", "Tackle");
    state.weather = w;
    state.weather_turns_left = (w == Weather::None) ? 0 : 5;
    FixedRNG rng(0.5f);
    auto events = engine.resolveTurn(state, UseMove{0}, UseMove{0}, rng);
    return damageOn(events, 1);
  };

  int surfDry = hit(Weather::None, "Inteleon", "Surf");
  int surfRain = hit(Weather::Rain, "Inteleon", "Surf");
  int surfSun = hit(Weather::Sun, "Inteleon", "Surf");
  REQUIRE(surfRain > surfDry);
  REQUIRE(surfRain >= surfDry * 3 / 2 - 2);
  REQUIRE(surfSun < surfDry);

  int fireDry = hit(Weather::None, "Infernape", "Flamethrower");
  int fireSun = hit(Weather::Sun, "Infernape", "Flamethrower");
  int fireRain = hit(Weather::Rain, "Infernape", "Flamethrower");
  REQUIRE(fireSun > fireDry);
  REQUIRE(fireRain < fireDry);
}

TEST_CASE("Sand chips 1/16 on non Rock/Ground/Steel only", "[weather][residual]") {
  DataLoader data;
  engine::test::loadAll(data);
  BattleEngine engine(data);

  auto state = makeDuel(data, "Luxray", "Growl", "Aerodactyl", "Sandstorm");
  state.weather = Weather::Sand;
  state.weather_turns_left = 5;
  int pikaHp = state.teams[0][0].currentHp;
  int ttarHp = state.teams[1][0].currentHp;

  FixedRNG rng(0.5f);
  auto events = engine.resolveTurn(state, UseMove{0}, UseMove{0}, rng);
  (void)events;

  REQUIRE(state.teams[0][0].currentHp == pikaHp - std::max(1, state.teams[0][0].stats.hp / 16));
  REQUIRE(state.teams[1][0].currentHp == ttarHp);
}

TEST_CASE("Garchomp (Ground) and Scizor (Steel) shrug off sand; snow never chips",
          "[weather][residual]") {
  DataLoader data;
  engine::test::loadAll(data);
  BattleEngine engine(data);

  auto chip = [&](const char *species, Weather w) {
    auto state = makeDuel(data, species, "Growl", "Snorlax", "Growl");
    state.weather = w;
    state.weather_turns_left = 5;
    int before = state.teams[0][0].currentHp;
    FixedRNG rng(0.5f);
    engine.resolveTurn(state, UseMove{0}, UseMove{0}, rng);
    return before - state.teams[0][0].currentHp;
  };

  REQUIRE(chip("Excadrill", Weather::Sand) == 0);
  REQUIRE(chip("Corviknight", Weather::Sand) == 0);
  REQUIRE(chip("MegaGengar", Weather::Sand) > 0);
  REQUIRE(chip("Excadrill", Weather::Snow) == 0);
  REQUIRE(chip("Luxray", Weather::Snow) == 0);
}

TEST_CASE("Weather chip resolves before status residuals", "[weather][residual]") {
  DataLoader data;
  engine::test::loadAll(data);
  BattleEngine engine(data);

  auto state = makeDuel(data, "Luxray", "Growl", "Snorlax", "Growl");
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
  engine::test::loadAll(data);
  BattleEngine engine(data);

  auto hit = [&](Weather w, const char *move) {
    auto state = makeDuel(data, "Inteleon", move, "Aerodactyl", "Growl");
    state.weather = w;
    state.weather_turns_left = (w == Weather::None) ? 0 : 5;
    FixedRNG rng(0.5f);
    auto events = engine.resolveTurn(state, UseMove{0}, UseMove{0}, rng);
    return damageOn(events, 1);
  };

  int surfDry = hit(Weather::None, "Surf");
  int surfSand = hit(Weather::Sand, "Surf");
  REQUIRE(surfSand < surfDry);

  int slamDry = hit(Weather::None, "BodySlam");
  int slamSand = hit(Weather::Sand, "BodySlam");
  REQUIRE(slamSand == slamDry);
}

TEST_CASE("SandStream and Drizzle set their weather on switch-in", "[weather][abilities]") {
  DataLoader data;
  engine::test::loadAll(data);
  overrideAbility(data, "Aerodactyl", "SandStream");
  overrideAbility(data, "Quagsire", "Drizzle");
  BattleEngine engine(data);

  BattleState state;
  state.teams[0][0] = buildCombatant(data, "Snorlax", {"Tackle"});
  state.teams[0][1] = buildCombatant(data, "Aerodactyl", {"StoneEdge"});
  state.teams[1][0] = buildCombatant(data, "Conkeldurr", {"CloseCombat"});
  state.team_size = {2, 1};

  FixedRNG rng(0.5f);
  auto events = engine.resolveTurn(state, SwitchAction{1}, UseMove{0}, rng);

  REQUIRE(countEvents<WeatherStartedEvent>(events) == 1);
  REQUIRE(state.weather == Weather::Sand);
}

TEST_CASE("startBattle: the slower weather ability wins the war", "[weather][abilities]") {
  DataLoader data;
  engine::test::loadAll(data);
  overrideAbility(data, "Aerodactyl", "SandStream");
  overrideAbility(data, "Quagsire", "Drizzle");
  BattleEngine engine(data);

  BattleState state;
  state.teams[0][0] = buildCombatant(data, "Aerodactyl", {"StoneEdge"});
  state.teams[1][0] = buildCombatant(data, "Quagsire", {"Surf"});
  state.team_size = {1, 1};

  FixedRNG srng(0.5f);
  auto events = engine.startBattle(state, srng);

  // Aerodactyl (faster) sets sand first, Quagsire overrides: rain stays.
  REQUIRE(countEvents<WeatherStartedEvent>(events) == 2);
  REQUIRE(state.weather == Weather::Rain);
}

TEST_CASE("Weather ability is silent if its weather is already up", "[weather][abilities]") {
  DataLoader data;
  engine::test::loadAll(data);
  BattleEngine engine(data);

  BattleState state;
  state.teams[0][0] = buildCombatant(data, "Snorlax", {"Tackle"});
  state.teams[0][1] = buildCombatant(data, "Quagsire", {"Surf"});
  state.teams[1][0] = buildCombatant(data, "Conkeldurr", {"CloseCombat"});
  state.team_size = {2, 1};
  state.weather = Weather::Rain;
  state.weather_turns_left = 5;

  FixedRNG rng(0.5f);
  auto events = engine.resolveTurn(state, SwitchAction{1}, UseMove{0}, rng);

  REQUIRE(countEvents<WeatherStartedEvent>(events) == 0);
  REQUIRE(state.weather_turns_left == 4);
}

TEST_CASE("validateState checks weather fields", "[weather][validate]") {
  DataLoader data;
  engine::test::loadAll(data);

  auto state = makeDuel(data, "Snorlax", "Tackle", "Conkeldurr", "CloseCombat");
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
    state.weather = Weather::Snow;
    state.weather_turns_left = 5;
    REQUIRE_NOTHROW(validateState(state, data));
  }
}
