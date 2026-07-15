#include "engine/battle_state.hpp"
#include "engine/data_loader.hpp"
#include "engine/engine.hpp"
#include "engine/events.hpp"
#include "engine/rng.hpp"
#include "engine/status.hpp"
#include "engine/validate.hpp"

#include <iostream>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

using namespace engine;

namespace {

BattlePokemon buildCombatant(const DataLoader &data, const std::string &speciesName, int level,
                             const std::vector<std::string> &moveNames) {
  BattlePokemon p;
  p.species_id = data.findSpeciesId(speciesName);
  if (p.species_id < 0)
    throw std::runtime_error("Unknown species: " + speciesName);

  const Species &sp = data.speciesByIndex(p.species_id);
  p.level = level;
  p.stats = computeSpeciesStats(sp, level);
  p.currentHp = p.stats.hp;

  for (size_t i = 0; i < moveNames.size() && i < kMaxMovesPerPokemon; ++i) {
    int mid = data.findMoveId(moveNames[i]);
    if (mid < 0)
      throw std::runtime_error("Unknown move: " + moveNames[i]);
    p.move_ids[i] = mid;
    p.pp[i] = data.moveByIndex(mid).pp;
  }
  return p;
}

void printEvent(const BattleEvent &ev, const BattleState &state, const DataLoader &data) {
  auto pokeName = [&](const CombatantRef &r) {
    const BattlePokemon &p =
        state.teams[static_cast<size_t>(r.side)][static_cast<size_t>(r.teamIndex)];
    return data.speciesByIndex(p.species_id).displayName;
  };

  std::visit(
      [&](auto &&e) {
        using T = std::decay_t<decltype(e)>;
        if constexpr (std::is_same_v<T, MoveUsedEvent>) {
          std::cout << "    " << pokeName(e.user) << " uses " << e.moveName << "!\n";
        } else if constexpr (std::is_same_v<T, DamageDealtEvent>) {
          std::cout << "    " << pokeName(e.target) << " takes " << e.damage << " damage";
          if (e.wasCrit)
            std::cout << " (critical hit!)";
          if (e.wasStab)
            std::cout << " (STAB)";
          if (e.effectiveness == 0.0f)
            std::cout << " (no effect)";
          else if (e.effectiveness >= 4.0f)
            std::cout << " (4x super effective!)";
          else if (e.effectiveness >= 2.0f)
            std::cout << " (super effective!)";
          else if (e.effectiveness <= 0.25f)
            std::cout << " (4x resisted)";
          else if (e.effectiveness < 1.0f)
            std::cout << " (not very effective)";
          std::cout << "\n";
        } else if constexpr (std::is_same_v<T, FaintedEvent>) {
          std::cout << "    " << pokeName(e.who) << " fainted!\n";
        } else if constexpr (std::is_same_v<T, MissedEvent>) {
          std::cout << "    " << pokeName(e.user) << " missed " << e.moveName << "!\n";
        } else if constexpr (std::is_same_v<T, StatusAppliedEvent>) {
          std::cout << "    " << pokeName(e.target) << " is afflicted with " << statusName(e.status)
                    << "!\n";
        } else if constexpr (std::is_same_v<T, StatusFailedEvent>) {
          std::cout << "    " << statusName(e.status) << " failed on " << pokeName(e.target)
                    << "!\n";
        } else if constexpr (std::is_same_v<T, StatusDamageEvent>) {
          std::cout << "    " << pokeName(e.target) << " takes " << e.damage << " damage from "
                    << statusName(e.status) << "\n";
        } else if constexpr (std::is_same_v<T, StatusCuredEvent>) {
          std::cout << "    " << pokeName(e.who) << " is no longer " << statusName(e.status)
                    << "\n";
        } else if constexpr (std::is_same_v<T, SwitchedOutEvent>) {
          std::cout << "    " << pokeName(e.who) << " withdraws!\n";
        } else if constexpr (std::is_same_v<T, SwitchedInEvent>) {
          std::cout << "    Go, " << pokeName(e.who) << "!\n";
        } else if constexpr (std::is_same_v<T, AbilityTriggeredEvent>) {
          std::cout << "    " << pokeName(e.who) << "'s " << e.ability << "!\n";
        } else if constexpr (std::is_same_v<T, StatStageChangedEvent>) {
          std::cout << "    " << pokeName(e.target) << (e.delta > 0 ? " gains " : " loses ")
                    << (e.delta > 0 ? e.delta : -e.delta) << " stage(s)!\n";
        } else if constexpr (std::is_same_v<T, StatChangeFailedEvent>) {
          std::cout << "    " << pokeName(e.target) << "'s stat can't go "
                    << (e.wasRaise ? "higher" : "lower") << "!\n";
        } else if constexpr (std::is_same_v<T, MoveFailedEvent>) {
          std::cout << "    " << pokeName(e.user) << "'s " << e.moveName << " failed!\n";
        } else if constexpr (std::is_same_v<T, WeatherStartedEvent>) {
          std::cout << "    Weather: " << weatherToString(e.weather) << " kicks in!\n";
        } else if constexpr (std::is_same_v<T, WeatherEndedEvent>) {
          std::cout << "    Weather: " << weatherToString(e.weather) << " subsided.\n";
        } else if constexpr (std::is_same_v<T, WeatherDamageEvent>) {
          std::cout << "    " << pokeName(e.target) << " is buffeted by the "
                    << weatherToString(e.weather) << " (" << e.damage << ")\n";
        } else if constexpr (std::is_same_v<T, HazardSetEvent>) {
          std::cout << "    " << hazardToString(e.hazard) << " scattered on side " << e.side
                    << " (layers: " << e.layers << ")\n";
        } else if constexpr (std::is_same_v<T, HazardDamageEvent>) {
          std::cout << "    " << pokeName(e.target) << " is hurt by " << hazardToString(e.hazard)
                    << " (" << e.damage << ")\n";
        } else if constexpr (std::is_same_v<T, HazardsClearedEvent>) {
          std::cout << "    Hazards cleared on side " << e.side << "\n";
        } else if constexpr (std::is_same_v<T, ToxicSpikesAbsorbedEvent>) {
          std::cout << "    " << pokeName(e.who) << " absorbed the ToxicSpikes!\n";
        } else if constexpr (std::is_same_v<T, HealedEvent>) {
          std::cout << "    " << pokeName(e.who) << " recovers " << e.amount << " HP!\n";
        } else if constexpr (std::is_same_v<T, RecoilDamageEvent>) {
          std::cout << "    " << pokeName(e.who) << " is hurt in return (" << e.damage << ")\n";
        } else if constexpr (std::is_same_v<T, ChargingEvent>) {
          std::cout << "    " << pokeName(e.who) << " is charging " << e.moveName << "...\n";
        } else if constexpr (std::is_same_v<T, ProtectedEvent>) {
          std::cout << "    " << pokeName(e.who) << " protected itself!\n";
        } else if constexpr (std::is_same_v<T, MoveSkippedEvent>) {
          const char *why = e.reason == SkipReason::Asleep     ? "is fast asleep"
                            : e.reason == SkipReason::Frozen   ? "is frozen solid"
                            : e.reason == SkipReason::Flinched ? "flinched"
                                                               : "is fully paralyzed";
          std::cout << "    " << pokeName(e.user) << " " << why << "!\n";
        }
      },
      ev);
}

struct Scenario {
  const char *title;
  const char *showcases;
  const char *species0;
  std::vector<std::string> moves0;
  const char *species1;
  std::vector<std::string> moves1;
  uint64_t seed;
  int maxTurns;
};

void runMatch(const Scenario &sc, const DataLoader &data, const BattleEngine &engine) {
  BattleState state;
  state.teams[0][0] = buildCombatant(data, sc.species0, 50, sc.moves0);
  state.teams[1][0] = buildCombatant(data, sc.species1, 50, sc.moves1);
  state.team_size = {1, 1};
  state.activeIndex = {0, 0};
  validateState(state, data);

  const Species &sp0 = data.speciesByIndex(state.teams[0][0].species_id);
  const Species &sp1 = data.speciesByIndex(state.teams[1][0].species_id);

  std::cout << "=== " << sc.title << " ===\n";
  std::cout << "    Showcases: " << sc.showcases << "\n";
  std::cout << "    " << sp0.displayName << " vs " << sp1.displayName << "\n\n";

  MersenneRNG rng(sc.seed);
  for (const auto &e : engine.startBattle(state, rng))
    printEvent(e, state, data);

  int turn = 0;
  while (!state.isOver() && turn < sc.maxTurns) {
    ++turn;
    std::cout << "  Turn " << turn << "  [HP " << sp0.displayName << " "
              << state.teams[0][0].currentHp << "/" << state.teams[0][0].stats.hp << " | "
              << sp1.displayName << " " << state.teams[1][0].currentHp << "/"
              << state.teams[1][0].stats.hp << "]\n";
    auto events = engine.resolveTurn(state, UseMove{0}, UseMove{0}, rng);
    for (const auto &e : events)
      printEvent(e, state, data);
    std::cout << "\n";
  }

  std::cout << "  Outcome: ";
  if (state.sideHasLost(0))
    std::cout << sp1.displayName << " wins";
  else if (state.sideHasLost(1))
    std::cout << sp0.displayName << " wins";
  else
    std::cout << "unresolved after " << turn << " turns";
  std::cout << " (" << state.turn << " turn" << (state.turn > 1 ? "s" : "") << ")\n\n";
}

void runSwitchShowcase(const DataLoader &data, const BattleEngine &engine) {
  BattleState state;
  state.teams[0][0] = buildCombatant(data, "gyarados", 100, {"UTurn"});
  state.teams[0][1] = buildCombatant(data, "machamp", 100, {"CloseCombat"});
  state.teams[1][0] = buildCombatant(data, "blastoise", 100, {"Surf"});
  state.teams[1][1] = buildCombatant(data, "snorlax", 100, {"BodySlam"});
  state.team_size = {2, 2};
  validateState(state, data);

  std::cout << "=== Match 8: Intimidate lead, U-Turn pivot, KO replacement ===\n";
  std::cout << "    Showcases: startBattle abilities, pivot switch, resolveReplacement\n";
  std::cout << "    Gyarados+Machamp vs Blastoise+Snorlax\n\n";

  MersenneRNG rng(7);
  for (const auto &e : engine.startBattle(state, rng))
    printEvent(e, state, data);

  int turn = 0;
  while (!state.isOver() && turn < 12) {
    ++turn;
    std::cout << "  Turn " << turn << "\n";
    // Turn 1: Gyarados pivots into Machamp; then spam slot 0 on both sides.
    Action a0 = (turn == 1) ? Action{UseMove{0, 1}} : Action{UseMove{0}};
    Action a1 = UseMove{0};
    auto events = engine.resolveTurn(state, a0, a1, rng);
    for (const auto &e : events)
      printEvent(e, state, data);

    for (int side = 0; side < kSideCount; ++side) {
      if (!state.isOver() && state.active(side).isFainted()) {
        for (int i = 0; i < state.team_size[static_cast<size_t>(side)]; ++i) {
          const BattlePokemon &p = state.teams[static_cast<size_t>(side)][static_cast<size_t>(i)];
          if (i != state.activeIndex[static_cast<size_t>(side)] && !p.isFainted()) {
            for (const auto &e : engine.resolveReplacement(state, side, i))
              printEvent(e, state, data);
            break;
          }
        }
      }
    }
    std::cout << "\n";
  }

  std::cout << "  Outcome: side " << (state.sideHasLost(0) ? 1 : 0) << " wins\n\n";
}

void runFieldShowcase(const DataLoader &data, const BattleEngine &engine) {
  BattleState state;
  state.teams[0][0] = buildCombatant(data, "tyranitar", 100, {"StealthRock", "StoneEdge"});
  state.teams[0][1] = buildCombatant(data, "scizor", 100, {"UTurn"});
  state.teams[1][0] = buildCombatant(data, "politoed", 100, {"Surf"});
  state.teams[1][1] = buildCombatant(data, "charizard", 100, {"Flamethrower"});
  state.team_size = {2, 2};
  validateState(state, data);

  std::cout << "=== Match 9: weather war, Stealth Rock, sand chip ===\n";
  std::cout << "    Showcases: Drizzle vs SandStream, hazards on switch, weather residuals\n";
  std::cout << "    Tyranitar+Scizor vs Politoed+Charizard\n\n";

  MersenneRNG rngStart(11);
  for (const auto &e : engine.startBattle(state, rngStart))
    printEvent(e, state, data);

  struct TurnScript {
    Action a0;
    Action a1;
  };
  const TurnScript script[] = {
      {UseMove{0}, UseMove{0}},      // Stealth Rock / Surf
      {UseMove{1}, SwitchAction{1}}, // Stone Edge / Charizard eats the rocks
      {SwitchAction{1}, UseMove{0}}, // Scizor comes in / Flamethrower (4x!)
      {UseMove{0}, UseMove{0}},      // U-Turn or fallback / Flamethrower
  };

  MersenneRNG rng(11);
  int turn = 0;
  for (const auto &step : script) {
    if (state.isOver())
      break;
    ++turn;
    std::cout << "  Turn " << turn << "\n";
    auto events = engine.resolveTurn(state, step.a0, step.a1, rng);
    for (const auto &e : events)
      printEvent(e, state, data);

    for (int side = 0; side < kSideCount; ++side) {
      if (!state.isOver() && state.active(side).isFainted()) {
        for (int i = 0; i < state.team_size[static_cast<size_t>(side)]; ++i) {
          const BattlePokemon &p = state.teams[static_cast<size_t>(side)][static_cast<size_t>(i)];
          if (i != state.activeIndex[static_cast<size_t>(side)] && !p.isFainted()) {
            for (const auto &e : engine.resolveReplacement(state, side, i))
              printEvent(e, state, data);
            break;
          }
        }
      }
    }
    std::cout << "\n";
  }
}

void runPhase89Showcase(const DataLoader &data, const BattleEngine &engine) {
  BattleState state;
  state.teams[0][0] = buildCombatant(data, "charizard", 100, {"SolarBeam", "Fly", "FlareBlitz"});
  state.teams[0][1] = buildCombatant(data, "snorlax", 100, {"Rest", "BodySlam"});
  state.teams[1][0] = buildCombatant(data, "blastoise", 100, {"Surf", "Protect"});
  state.teams[1][1] = buildCombatant(data, "garchomp", 100, {"StoneEdge"});
  state.team_size = {2, 2};
  validateState(state, data);

  std::cout << "=== Match 10: two-turn moves, Protect, Rest, recoil, crits ===\n";
  std::cout << "    Showcases: SolarBeam charge, Fly invulnerability, Protect, Rest, Flare Blitz "
               "recoil\n";
  std::cout << "    Charizard+Snorlax vs Blastoise+Garchomp\n\n";

  MersenneRNG rngStart(3);
  for (const auto &e : engine.startBattle(state, rngStart))
    printEvent(e, state, data);

  struct TurnScript {
    Action a0;
    Action a1;
  };
  const TurnScript script[] = {
      {UseMove{0}, UseMove{1}}, // SolarBeam charges / Blastoise Protects
      {UseMove{0}, UseMove{0}}, // SolarBeam fires (super effective) / Surf
      {UseMove{1}, UseMove{1}}, // Fly (up) / Protect whiffs on the airborne target
      {UseMove{1}, UseMove{0}}, // Fly strikes / Surf
  };

  MersenneRNG rng(5);
  int turn = 0;
  for (const auto &step : script) {
    if (state.isOver())
      break;
    ++turn;
    std::cout << "  Turn " << turn << "\n";
    for (const auto &e : engine.resolveTurn(state, step.a0, step.a1, rng))
      printEvent(e, state, data);

    for (int side = 0; side < kSideCount; ++side) {
      if (!state.isOver() && state.active(side).isFainted()) {
        for (int i = 0; i < state.team_size[static_cast<size_t>(side)]; ++i) {
          const BattlePokemon &p = state.teams[static_cast<size_t>(side)][static_cast<size_t>(i)];
          if (i != state.activeIndex[static_cast<size_t>(side)] && !p.isFainted()) {
            for (const auto &e : engine.resolveReplacement(state, side, i))
              printEvent(e, state, data);
            break;
          }
        }
      }
    }
    std::cout << "\n";
  }
}

} // namespace

int main() {
  try {
    DataLoader data;
    data.loadAll(BATTLE_ENGINE_DATA_DIR);

    std::cout << "Battle engine demo  -  " << data.speciesCount() << " species, "
              << data.moveCount() << " moves loaded\n\n";

    const std::vector<Scenario> scenarios = {
        {"Match 1: STAB super-effective",
         "STAB (x1.5) + super-effective (x2) on a Grass/Poison target",
         "charizard",
         {"Flamethrower"},
         "venusaur",
         {"VineWhip"},
         42,
         10},

        {"Match 2: 4x weakness via dual type",
         "Rock vs Fire/Flying = 2x * 2x = 4x; one-shot OHKO",
         "garchomp",
         {"StoneEdge"},
         "charizard",
         {"AirSlash"},
         42,
         5},

        {"Match 3: priority bracket beats raw speed",
         "Pikachu (Spd 95) uses QuickAttack (+1 priority) before Gengar (Spd 115); Normal hits "
         "Ghost for 0",
         "pikachu",
         {"QuickAttack"},
         "gengar",
         {"ShadowBall"},
         42,
         3},

        {"Match 4: accuracy roll, miss event",
         "Machamp's StoneEdge has 80% accuracy; seed 2 forces a miss on turn 1",
         "machamp",
         {"StoneEdge"},
         "blastoise",
         {"Surf"},
         2,
         5},

        {"Match 5: bulk vs frailty",
         "Snorlax (220 HP, 115 SpD) sponges a Thunderbolt; Pikachu (95 HP, 45 Def) folds to "
         "BodySlam",
         "snorlax",
         {"BodySlam"},
         "pikachu",
         {"Thunderbolt"},
         7,
         5},

        {"Match 6: burn cripples a physical attacker",
         "WillOWisp burns Machamp: CloseCombat halved (resisted on Fire/Flying) + 1/16 chip per "
         "turn",
         "charizard",
         {"WillOWisp"},
         "machamp",
         {"CloseCombat"},
         42,
         5},

        {"Match 7: Spore, sleep turns, wake-up",
         "Venusaur sleeps Snorlax (1-3 turns); Snorlax skips, wakes, gets re-Spored",
         "venusaur",
         {"Spore"},
         "snorlax",
         {"BodySlam"},
         42,
         6},
    };

    BattleEngine engine(data);
    for (const auto &sc : scenarios) {
      runMatch(sc, data, engine);
    }
    runSwitchShowcase(data, engine);
    runFieldShowcase(data, engine);
    runPhase89Showcase(data, engine);

    return 0;
  } catch (const std::exception &e) {
    std::cerr << "Error: " << e.what() << "\n";
    return 1;
  }
}
