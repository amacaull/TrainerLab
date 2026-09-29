#include "engine/core/battle_state.hpp"
#include "engine/core/data_loader.hpp"
#include "engine/core/engine.hpp"
#include "engine/core/events.hpp"
#include "engine/core/rng.hpp"
#include "engine/core/validate.hpp"
#include "engine/model/status.hpp"

#include <iostream>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

using namespace engine;

namespace {

BattlePokemon buildCombatant(const DataLoader &data, const std::string &speciesName,
                             const std::vector<std::string> &moveNames) {
  BattlePokemon p;
  p.species_id = data.findSpeciesId(speciesName);
  if (p.species_id < 0)
    throw std::runtime_error("Unknown species: " + speciesName);

  const Species &sp = data.speciesByIndex(p.species_id);
  p.level = kBattleLevel;
  p.stats = computeSpeciesStats(sp, kBattleLevel);
  p.currentHp = p.stats.hp;
  p.item_id = sp.item.empty() ? kNoItem : data.findItemId(sp.item);

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
        } else if constexpr (std::is_same_v<T, ItemTriggeredEvent>) {
          std::cout << "    " << pokeName(e.who) << "'s " << e.itemName << " activates!\n";
        } else if constexpr (std::is_same_v<T, ItemConsumedEvent>) {
          std::cout << "    " << pokeName(e.who) << " used up its " << e.itemName << "!\n";
        } else if constexpr (std::is_same_v<T, ItemDamageEvent>) {
          std::cout << "    " << pokeName(e.who) << " is hurt by its " << e.itemName << " ("
                    << e.damage << ")\n";
        } else if constexpr (std::is_same_v<T, AbilityDamageEvent>) {
          std::cout << "    " << pokeName(e.who) << " is hurt by its " << e.ability << " ("
                    << e.damage << ")\n";
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
  state.teams[0][0] = buildCombatant(data, sc.species0, sc.moves0);
  state.teams[1][0] = buildCombatant(data, sc.species1, sc.moves1);
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
  state.teams[0][0] = buildCombatant(data, "Zarude", {"UTurn"});
  state.teams[0][1] = buildCombatant(data, "Luxray", {"WildCharge"});
  state.teams[1][0] = buildCombatant(data, "Inteleon", {"HydroPump"});
  state.teams[1][1] = buildCombatant(data, "Snorlax", {"BodySlam"});
  state.team_size = {2, 2};
  validateState(state, data);

  std::cout << "=== Match 8: U-Turn pivot into an Intimidate switch-in, KO replacement ===\n";
  std::cout << "    Showcases: pivot switch, ability on entry, resolveReplacement\n";
  std::cout << "    Zarude+Luxray vs Inteleon+Snorlax\n\n";

  MersenneRNG rng(7);
  for (const auto &e : engine.startBattle(state, rng))
    printEvent(e, state, data);

  int turn = 0;
  while (!state.isOver() && turn < 12) {
    ++turn;
    std::cout << "  Turn " << turn << "\n";
    // Turn 1: Zarude U-turns into Luxray, whose Intimidate greets Inteleon.
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
  state.teams[0][0] = buildCombatant(data, "NinetalesAlola", {"AuroraVeil", "Blizzard"});
  state.teams[0][1] = buildCombatant(data, "Mamoswine", {"IcicleCrash", "Earthquake"});
  state.teams[1][0] = buildCombatant(data, "Aerodactyl", {"StealthRock", "StoneEdge"});
  state.teams[1][1] = buildCombatant(data, "Infernape", {"FlareBlitz"});
  state.team_size = {2, 2};
  validateState(state, data);

  std::cout << "=== Match 9: snow, Aurora Veil, SlushRush, Stealth Rock ===\n";
  std::cout << "    Showcases: weather from an ability, screen halving, doubled speed in snow, "
               "hazards on switch\n";
  std::cout << "    NinetalesAlola+Mamoswine vs Aerodactyl+Infernape\n\n";

  MersenneRNG rngStart(11);
  for (const auto &e : engine.startBattle(state, rngStart))
    printEvent(e, state, data);

  struct TurnScript {
    Action a0;
    Action a1;
  };
  const TurnScript script[] = {
      {UseMove{0}, UseMove{0}},      // Aurora Veil (snow is already up) / Stealth Rock
      {SwitchAction{1}, UseMove{1}}, // Mamoswine eats the rocks / Stone Edge, halved by the veil
      {UseMove{0}, UseMove{1}},      // SlushRush: Mamoswine strikes first / Stone Edge
      {UseMove{0}, SwitchAction{1}}, // Mamoswine is Choice-locked / Infernape onto the rocks
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

void runMechanicsShowcase(const DataLoader &data, const BattleEngine &engine) {
  BattleState state;
  state.teams[0][0] = buildCombatant(data, "Dragapult", {"PhantomForce"});
  state.teams[0][1] = buildCombatant(data, "Weavile", {"TripleAxel"});
  state.teams[1][0] = buildCombatant(data, "Toxapex", {"BanefulBunker", "Scald"});
  state.teams[1][1] = buildCombatant(data, "Infernape", {"FlareBlitz"});
  state.team_size = {2, 2};
  validateState(state, data);

  std::cout << "=== Match 10: Phantom Force, Baneful Bunker, multi-hit ===\n";
  std::cout << "    Showcases: two-turn vanish, protection pierced, Triple Axel's escalating "
               "20/40/60 volley\n";
  std::cout << "    Dragapult+Weavile vs Toxapex+Infernape\n\n";

  MersenneRNG rngStart(3);
  for (const auto &e : engine.startBattle(state, rngStart))
    printEvent(e, state, data);

  struct TurnScript {
    Action a0;
    Action a1;
  };
  const TurnScript script[] = {
      {UseMove{0}, UseMove{1}},      // Dragapult vanishes / Scald finds nobody home
      {UseMove{0}, UseMove{0}},      // Phantom Force lands THROUGH Baneful Bunker
      {SwitchAction{1}, UseMove{1}}, // Weavile comes in / Scald
      {UseMove{0}, UseMove{1}},      // Triple Axel: three hits, rising power / Scald
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
        {"Match 1: STAB, super-effective, recoil",
         "Fire STAB (x1.5) x2 on Flying/Steel; both sides pay 1/3 recoil",
         "Infernape",
         {"FlareBlitz"},
         "Corviknight",
         {"BraveBird"},
         42,
         10},

        {"Match 2: 4x weakness via dual type",
         "Fighting vs Dark/Ice = 2x * 2x = 4x; Drain Punch heals half of it back",
         "Conkeldurr",
         {"DrainPunch"},
         "Weavile",
         {"KnockOff"},
         42,
         5},

        {"Match 3: priority bracket beats raw speed",
         "Conkeldurr (126) throws Mach Punch (+1) before Mega Gengar (394) - and Fighting "
         "hits Ghost for 0 anyway",
         "Conkeldurr",
         {"MachPunch"},
         "MegaGengar",
         {"ShadowBall"},
         42,
         3},

        {"Match 4: accuracy roll, miss event",
         "Two 80%-accuracy moves trade blows: Stone Edge against Hydro Pump",
         "Aerodactyl",
         {"StoneEdge"},
         "Inteleon",
         {"HydroPump"},
         2,
         5},

        {"Match 5: bulk vs frailty",
         "Snorlax sponges Wild Charge and answers with Body Slam; Luxray also pays the recoil",
         "Snorlax",
         {"BodySlam"},
         "Luxray",
         {"WildCharge"},
         7,
         5},

        {"Match 6: burn cripples a physical attacker",
         "Mega Sableye burns Excadrill: Earthquake halved + 1/16 chip every turn",
         "MegaSableye",
         {"WillOWisp"},
         "Excadrill",
         {"Earthquake"},
         42,
         5},

        {"Match 7: the Toxic ladder",
         "Quagsire poisons Snorlax badly: 1/16, 2/16, 3/16... and a second Toxic fails",
         "Quagsire",
         {"Toxic"},
         "Snorlax",
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
    runMechanicsShowcase(data, engine);

    return 0;
  } catch (const std::exception &e) {
    std::cerr << "Error: " << e.what() << "\n";
    return 1;
  }
}
