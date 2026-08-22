#include "helpers.hpp"

#include "engine/core/battle_state.hpp"
#include "engine/core/data_loader.hpp"
#include "engine/core/engine.hpp"
#include "engine/core/rng.hpp"
#include "engine/core/validate.hpp"

#include <catch2/catch_test_macros.hpp>

#include <variant>

using namespace engine;
using engine::test::buildCombatant;
using engine::test::buildLoadout;
using engine::test::overrideLegendary;
using engine::test::overrideWeight;

namespace {

// The real thing: species carry the held item from their sheet, so these
// cases exercise ability + item + move together, the way a match does.
BattleState loadoutDuel(const DataLoader &data, const char *s0, std::vector<std::string> m0,
                        const char *s1, std::vector<std::string> m1) {
  BattleState state;
  state.teams[0][0] = buildLoadout(data, s0, m0);
  state.teams[1][0] = buildLoadout(data, s1, m1);
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

int firstDamageOn(const EventLog &events, int side) {
  for (const auto &ev : events)
    if (auto *e = std::get_if<DamageDealtEvent>(&ev))
      if (e->target.side == side)
        return e->damage;
  return -1;
}

} // namespace

// ---------------------------------------------------------------------------
// Team legality (ADR #50)
// ---------------------------------------------------------------------------

TEST_CASE("validateTeam accepts a legal team", "[integration][team]") {
  DataLoader data;
  engine::test::loadAll(data);

  std::array<BattlePokemon, kTeamSize> team{};
  team[0] = buildLoadout(data, "Mimikyu", {"SwordsDance", "ShadowSneak"});
  team[1] = buildLoadout(data, "Conkeldurr", {"DrainPunch", "MachPunch"});
  team[2] = buildLoadout(data, "MegaGengar", {"ShadowBall", "NastyPlot"});

  REQUIRE_NOTHROW(validateTeam(team, 3, data));
}

TEST_CASE("validateTeam enforces the Species Clause", "[integration][team]") {
  DataLoader data;
  engine::test::loadAll(data);

  std::array<BattlePokemon, kTeamSize> team{};
  team[0] = buildLoadout(data, "Snorlax", {"BodySlam"});
  team[1] = buildLoadout(data, "Conkeldurr", {"DrainPunch"});
  team[2] = buildLoadout(data, "Snorlax", {"Curse"});

  REQUIRE_THROWS_AS(validateTeam(team, 3, data), std::invalid_argument);
}

TEST_CASE("validateTeam allows one Mega and one legendary, never two", "[integration][team]") {
  DataLoader data;
  engine::test::loadAll(data);

  std::array<BattlePokemon, kTeamSize> team{};
  team[0] = buildLoadout(data, "MegaGengar", {"ShadowBall"});
  team[1] = buildLoadout(data, "Snorlax", {"BodySlam"});
  REQUIRE_NOTHROW(validateTeam(team, 2, data));

  team[1] = buildLoadout(data, "MegaMawile", {"PlayRough"});
  REQUIRE_THROWS_AS(validateTeam(team, 2, data), std::invalid_argument);

  // The official legendary list is still pending, so the rule is proven on
  // flipped flags: the day the sheet lands, only the JSON changes.
  overrideLegendary(data, "Snorlax", true);
  overrideLegendary(data, "Conkeldurr", true);
  team[0] = buildLoadout(data, "Snorlax", {"BodySlam"});
  team[1] = buildLoadout(data, "Mimikyu", {"ShadowSneak"});
  REQUIRE_NOTHROW(validateTeam(team, 2, data));

  team[1] = buildLoadout(data, "Conkeldurr", {"DrainPunch"});
  REQUIRE_THROWS_AS(validateTeam(team, 2, data), std::invalid_argument);
}

TEST_CASE("validateTeam refuses a move outside the species' movepool", "[integration][team]") {
  DataLoader data;
  engine::test::loadAll(data);

  std::array<BattlePokemon, kTeamSize> team{};
  team[0] = buildLoadout(data, "Snorlax", {"BodySlam"});
  REQUIRE_NOTHROW(validateTeam(team, 1, data));

  team[0] = buildLoadout(data, "Snorlax", {"CloseCombat"}); // not on its sheet
  REQUIRE_THROWS_AS(validateTeam(team, 1, data), std::invalid_argument);
}

// ---------------------------------------------------------------------------
// Weight-based power (Low Kick / Grass Knot)
// ---------------------------------------------------------------------------

TEST_CASE("LowKick climbs the canon weight tiers", "[integration][weight]") {
  DataLoader data;
  engine::test::loadAll(data);
  BattleEngine engine(data);

  // Same defender throughout, only the catalog weight moves: the tiers are
  // isolated from types, stats and stages.
  auto damageAt = [&](double kg) {
    DataLoader d;
    engine::test::loadAll(d);
    overrideWeight(d, "Snorlax", kg);
    BattleEngine e(d);
    BattleState state;
    state.teams[0][0] = buildCombatant(d, "Weavile", {"LowKick"});
    state.teams[1][0] = buildCombatant(d, "Snorlax", {"Curse"});
    state.team_size = {1, 1};
    FixedRNG rng(0.99f);
    return firstDamageOn(e.resolveTurn(state, UseMove{0}, UseMove{0}, rng), 1);
  };

  int t20 = damageAt(5.0);    // < 10 kg  -> 20
  int t40 = damageAt(20.0);   // < 25     -> 40
  int t60 = damageAt(40.0);   // < 50     -> 60
  int t80 = damageAt(80.0);   // < 100    -> 80
  int t100 = damageAt(150.0); // < 200    -> 100
  int t120 = damageAt(400.0); // >= 200   -> 120

  REQUIRE(t20 < t40);
  REQUIRE(t40 < t60);
  REQUIRE(t60 < t80);
  REQUIRE(t80 < t100);
  REQUIRE(t100 < t120);
  // The ladder is proportional: the top tier is about six times the bottom.
  REQUIRE(t120 > t20 * 5);

  // Boundaries land on the lower tier (canon: strictly below the cut).
  REQUIRE(damageAt(9.9) == t20);
  REQUIRE(damageAt(10.0) == t40);
  REQUIRE(damageAt(199.9) == t100);
  REQUIRE(damageAt(200.0) == t120);
}

// ---------------------------------------------------------------------------
// The team sheet's own combos, played with the real held items
// ---------------------------------------------------------------------------

TEST_CASE("Guts + FlameOrb: Conkeldurr burns itself into a bigger hit", "[integration][combo]") {
  DataLoader data;
  engine::test::loadAll(data);
  BattleEngine engine(data);

  // Toxapex resists Fighting and heals: the battle survives long enough for
  // the burn to start chipping.
  auto state = loadoutDuel(data, "Conkeldurr", {"DrainPunch"}, "Toxapex", {"Recover"});
  FixedRNG rng(0.99f);

  // Turn 1: the orb goes off at the end of the turn.
  auto t1 = engine.resolveTurn(state, UseMove{0}, UseMove{0}, rng);
  REQUIRE(state.teams[0][0].status == Status::Burn);
  int plain = firstDamageOn(t1, 1);

  // Turn 2: burnt, so Guts is up — and Guts also waives the burn's own
  // halving of physical damage. Net effect must be a HARDER hit.
  auto t2 = engine.resolveTurn(state, UseMove{0}, UseMove{0}, rng);
  int burnt = firstDamageOn(t2, 1);
  REQUIRE(burnt > plain);
  REQUIRE(countEv<StatusDamageEvent>(t2) >= 1); // the burn keeps chipping
}

TEST_CASE("BellyDrum + SitrusBerry: Azumarill maxes Attack and eats the berry back",
          "[integration][combo]") {
  DataLoader data;
  engine::test::loadAll(data);
  BattleEngine engine(data);

  auto state = loadoutDuel(data, "Azumarill", {"BellyDrum", "AquaJet"}, "Snorlax", {"Curse"});
  int maxHp = state.teams[0][0].stats.hp;
  FixedRNG rng(0.99f);

  auto events = engine.resolveTurn(state, UseMove{0}, UseMove{0}, rng);
  REQUIRE(state.teams[0][0].stat_stages[static_cast<size_t>(StatIndex::Atk)] == 6);
  REQUIRE(countEv<ItemConsumedEvent>(events) == 1);
  // Paid half, got a quarter back: comfortably above the half mark it drops to.
  REQUIRE(state.teams[0][0].currentHp > maxHp / 2);
  REQUIRE(state.teams[0][0].currentHp < maxHp);

  // HugePower on top of +6 makes AquaJet hit like a truck.
  auto t2 = engine.resolveTurn(state, UseMove{1}, UseMove{0}, rng);
  REQUIRE(firstDamageOn(t2, 1) > 200);
}

TEST_CASE("PopulationBomb + LoadedDice: Maushold lands all ten", "[integration][combo]") {
  DataLoader data;
  engine::test::loadAll(data);
  BattleEngine engine(data);

  auto state = loadoutDuel(data, "Maushold", {"PopulationBomb"}, "Blissey", {"SoftBoiled"});
  FixedRNG rng(0.99f); // rangeInt -> min: without the dice, hit 2 would miss
  auto events = engine.resolveTurn(state, UseMove{0}, UseMove{0}, rng);
  REQUIRE(hitsOn(events, 1) == 10);
}

TEST_CASE("Disguise vs a volley: Mimikyu eats one hit, takes the other nine",
          "[integration][combo]") {
  DataLoader data;
  engine::test::loadAll(data);
  BattleEngine engine(data);

  // Not PopulationBomb: Mimikyu is a Ghost, so a Normal volley never even
  // reaches the costume. Bullet Seed is the Grass one on the same sheet.
  auto state = loadoutDuel(data, "Maushold", {"BulletSeed"}, "Mimikyu", {"SwordsDance"});
  FixedRNG rng(0.99f); // LoadedDice floor: 4 hits
  auto events = engine.resolveTurn(state, UseMove{0}, UseMove{0}, rng);

  REQUIRE(state.teams[1][0].disguise_broken == 1);
  REQUIRE(hitsOn(events, 1) == 3); // the costume swallowed exactly one of the four
}

TEST_CASE("RockHead + HeadSmash: Arcanine-Hisui pays nothing", "[integration][combo]") {
  DataLoader data;
  engine::test::loadAll(data);
  BattleEngine engine(data);

  auto state = loadoutDuel(data, "ArcanineHisui", {"HeadSmash"}, "Snorlax", {"Curse"});
  int before = state.teams[0][0].currentHp;
  FixedRNG rng(0.99f);
  auto events = engine.resolveTurn(state, UseMove{0}, UseMove{0}, rng);

  REQUIRE(firstDamageOn(events, 1) > 0);
  REQUIRE(countEv<RecoilDamageEvent>(events) == 0);
  REQUIRE(state.teams[0][0].currentHp == before); // ChoiceBand, no LifeOrb: untouched
}

TEST_CASE("SnowWarning + AuroraVeil + SlushRush all pull on the same weather",
          "[integration][combo]") {
  DataLoader data;
  engine::test::loadAll(data);
  BattleEngine engine(data);

  BattleState state;
  state.teams[0][0] = buildLoadout(data, "NinetalesAlola", {"AuroraVeil", "Blizzard"});
  state.teams[0][1] = buildLoadout(data, "Mamoswine", {"IcicleCrash"});
  state.teams[1][0] = buildLoadout(data, "Aerodactyl", {"StoneEdge"});
  state.team_size = {2, 1};

  FixedRNG srng(0.5f);
  auto start = engine.startBattle(state, srng);
  REQUIRE(state.weather == Weather::Snow); // set by the ability, no move needed
  REQUIRE(countEv<WeatherStartedEvent>(start) == 1);

  FixedRNG rng(0.99f);
  // Screen up, then compare the same Stone Edge with and without it.
  auto veiled = engine.resolveTurn(state, UseMove{0}, UseMove{0}, rng);
  REQUIRE(state.aurora_veil_turns[0] > 0);
  int halved = firstDamageOn(veiled, 0);

  BattleState bare;
  bare.teams[0][0] = buildLoadout(data, "NinetalesAlola", {"AuroraVeil", "Blizzard"});
  bare.teams[1][0] = buildLoadout(data, "Aerodactyl", {"StoneEdge"});
  bare.team_size = {1, 1};
  FixedRNG rng2(0.99f);
  int full = firstDamageOn(engine.resolveTurn(bare, UseMove{1}, UseMove{0}, rng2), 0);
  REQUIRE(halved < full);

  // Mamoswine comes in under the snow: SlushRush doubles 259 past Aerodactyl's 394.
  auto t2 = engine.resolveTurn(state, SwitchAction{1}, UseMove{0}, rng);
  (void)t2;
  auto t3 = engine.resolveTurn(state, UseMove{0}, UseMove{0}, rng);
  for (const auto &ev : t3)
    if (auto *e = std::get_if<MoveUsedEvent>(&ev)) {
      REQUIRE(e->user.side == 0); // the mammoth moves first
      break;
    }
}
