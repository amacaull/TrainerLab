#include "engine/ffi/ffi.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <stdexcept>
#include <string>
#include <vector>

using namespace engine;
using namespace engine::ffi;

namespace {
void ensureInit() { engine_init(BATTLE_ENGINE_DATA_DIR); }

template <typename F> std::string messageOf(F &&f) {
  try {
    f();
  } catch (const std::exception &e) {
    return e.what();
  }
  return {};
}

bool startsWith(const std::string &s, const char *prefix) { return s.rfind(prefix, 0) == 0; }

// Built the way callers must: make_combatant only.
BattleState battle(const std::vector<const char *> &left, const std::vector<const char *> &right) {
  BattleState s;
  const std::vector<const char *> *teams[2] = {&left, &right};
  for (size_t side = 0; side < 2; ++side) {
    for (size_t i = 0; i < teams[side]->size(); ++i)
      s.teams[side][i] = make_combatant(find_species_id((*teams[side])[i]));
    s.team_size[side] = static_cast<int>(teams[side]->size());
  }
  return s;
}

int moveSlot(const BattlePokemon &p, const char *name) {
  const int id = find_move_id(name);
  for (int i = 0; i < kMaxMovesPerPokemon; ++i)
    if (p.move_ids[static_cast<size_t>(i)] == id)
      return i;
  FAIL("move " << name << " not in the set");
  return -1;
}

std::vector<int> moveSlots(const std::vector<FfiAction> &actions) {
  std::vector<int> out;
  for (const FfiAction &a : actions)
    if (a.kind == 0)
      out.push_back(a.index);
  return out;
}

std::vector<int> switchTargets(const std::vector<FfiAction> &actions) {
  std::vector<int> out;
  for (const FfiAction &a : actions)
    if (a.kind == 1)
      out.push_back(a.index);
  return out;
}

FfiAction useMove(int slot) { return FfiAction{0, slot, -1}; }
} // namespace

TEST_CASE("legal_actions lists every usable move and every valid switch", "[legal]") {
  ensureInit();
  BattleState s = battle({"Dragapult", "Snorlax", "Toxapex"}, {"Blissey"});
  start_battle(s, 1);

  SECTION("a fresh Pokemon has its four moves, and the healthy bench") {
    const auto actions = legal_actions(s, 0);
    REQUIRE(moveSlots(actions) == std::vector<int>{0, 1, 2, 3});
    REQUIRE(switchTargets(actions) == std::vector<int>{1, 2});
  }

  SECTION("a slot with no PP is left out") {
    s.teams[0][0].pp[2] = 0;
    REQUIRE(moveSlots(legal_actions(s, 0)) == std::vector<int>{0, 1, 3});
  }

  SECTION("a fainted benched Pokemon is not a switch target") {
    s.teams[0][2].currentHp = 0;
    REQUIRE(switchTargets(legal_actions(s, 0)) == std::vector<int>{1});
  }

  SECTION("a lone Pokemon has no switch") { REQUIRE(switchTargets(legal_actions(s, 1)).empty()); }

  SECTION("no duplicates") {
    const auto actions = legal_actions(s, 0);
    for (size_t i = 0; i < actions.size(); ++i)
      for (size_t j = i + 1; j < actions.size(); ++j)
        REQUIRE_FALSE((actions[i].kind == actions[j].kind && actions[i].index == actions[j].index));
  }
}

TEST_CASE("legal_actions follows the Choice lock", "[legal]") {
  ensureInit();
  BattleState s = battle({"Dragapult", "Snorlax"}, {"Blissey"});
  start_battle(s, 1);
  BattlePokemon &dragapult = s.teams[0][0];
  const int fang = moveSlot(dragapult, "FireFang");
  dragapult.locked_move_id = dragapult.move_ids[static_cast<size_t>(fang)];

  SECTION("only the locked move, and switching still frees it") {
    const auto actions = legal_actions(s, 0);
    REQUIRE(moveSlots(actions) == std::vector<int>{fang});
    REQUIRE(switchTargets(actions) == std::vector<int>{1});
  }

  SECTION("a locked move out of PP leaves Struggle, as a single entry") {
    dragapult.pp[static_cast<size_t>(fang)] = 0;
    const auto actions = legal_actions(s, 0);
    REQUIRE(moveSlots(actions).size() == 1);
    REQUIRE(switchTargets(actions) == std::vector<int>{1});
  }
}

TEST_CASE("legal_actions collapses the forced cases to one move entry", "[legal]") {
  ensureInit();

  SECTION("Struggle: no usable PP anywhere") {
    BattleState s = battle({"Snorlax", "Toxapex"}, {"Blissey"});
    start_battle(s, 1);
    s.teams[0][0].pp = {0, 0, 0, 0};
    const auto actions = legal_actions(s, 0);
    REQUIRE(moveSlots(actions).size() == 1);
    REQUIRE(switchTargets(actions) == std::vector<int>{1});
    REQUIRE_NOTHROW(resolve_turn(s, actions[0], legal_actions(s, 1)[0], 7));
  }

  SECTION("charging: one entry naming the charging move, no switch") {
    BattleState s = battle({"Dragapult", "Snorlax"}, {"Blissey"});
    start_battle(s, 1);
    const int phantom = moveSlot(s.teams[0][0], "PhantomForce");
    resolve_turn(s, useMove(phantom), legal_actions(s, 1)[0], 3);
    REQUIRE(s.active(0).charging_move_id != kNoMove);

    const auto actions = legal_actions(s, 0);
    REQUIRE(actions.size() == 1);
    REQUIRE(actions[0].kind == 0);
    REQUIRE(actions[0].index == phantom);
  }
}

TEST_CASE("a fainted active has replacements, not actions", "[legal]") {
  ensureInit();
  BattleState s = battle({"Dragapult", "Snorlax", "Toxapex"}, {"Blissey"});
  start_battle(s, 1);

  SECTION("standing: actions, no replacement") {
    REQUIRE_FALSE(legal_actions(s, 0).empty());
    REQUIRE(legal_replacements(s, 0).empty());
  }

  SECTION("fainted: no action, every healthy benched Pokemon") {
    s.teams[0][0].currentHp = 0;
    s.teams[0][2].currentHp = 0;
    REQUIRE(legal_actions(s, 0).empty());
    REQUIRE(legal_replacements(s, 0) == std::vector<int>{1});
  }

  SECTION("each replacement is accepted") {
    s.teams[0][0].currentHp = 0;
    for (int target : legal_replacements(s, 0)) {
      BattleState probe = s;
      REQUIRE_NOTHROW(resolve_replacement(probe, 0, target));
    }
  }
}

TEST_CASE("legal_actions, legal_replacements and observe reject a bad side", "[legal][observe]") {
  ensureInit();
  BattleState s = battle({"Dragapult"}, {"Blissey"});
  REQUIRE(startsWith(messageOf([&] { return legal_actions(s, 2); }), "E_ARG:"));
  REQUIRE(startsWith(messageOf([&] { return legal_replacements(s, -1); }), "E_ARG:"));
  REQUIRE(startsWith(messageOf([&] { return observe(s, 2); }), "E_ARG:"));
}

TEST_CASE("start_battle and every switch reveal the Pokemon that enters", "[observe]") {
  ensureInit();
  BattleState s = battle({"Dragapult", "Snorlax"}, {"Blissey", "Toxapex"});
  REQUIRE(s.teams[0][0].revealed == 0);

  start_battle(s, 1);
  REQUIRE(s.teams[0][0].revealed == 1);
  REQUIRE(s.teams[1][0].revealed == 1);
  REQUIRE(s.teams[0][1].revealed == 0);
  REQUIRE(s.teams[1][1].revealed == 0);

  resolve_turn(s, FfiAction{1, 1, -1}, legal_actions(s, 1)[0], 2);
  REQUIRE(s.teams[0][1].revealed == 1);

  SECTION("and it stays revealed after leaving") {
    resolve_turn(s, FfiAction{1, 0, -1}, legal_actions(s, 1)[0], 3);
    REQUIRE(s.teams[0][1].revealed == 1);
  }
}

TEST_CASE("observe hides the unrevealed opponents and nothing else", "[observe]") {
  ensureInit();
  BattleState s = battle({"Dragapult", "Snorlax"}, {"Blissey", "Toxapex"});
  start_battle(s, 1);
  resolve_turn(s, legal_actions(s, 0)[0], legal_actions(s, 1)[0], 2);
  const BattleState view = observe(s, 0);

  SECTION("an opponent that never entered is reset; the team size stays") {
    REQUIRE(view.teams[1][1] == BattlePokemon{});
    REQUIRE(view.team_size[1] == 2);
  }

  SECTION("own bench, both actives and the field are exact") {
    REQUIRE(view.teams[0][0] == s.teams[0][0]);
    REQUIRE(view.teams[0][1] == s.teams[0][1]);
    REQUIRE(view.teams[1][0] == s.teams[1][0]);
    REQUIRE(view.activeIndex == s.activeIndex);
    REQUIRE(view.turn == s.turn);
    REQUIRE(view.weather == s.weather);
    REQUIRE(view.hazards[0].spikes == s.hazards[0].spikes);
  }

  SECTION("each side's view hides the other side") {
    REQUIRE(observe(s, 1).teams[0][1] == BattlePokemon{});
    REQUIRE(observe(s, 1).teams[1][1] == s.teams[1][1]);
  }

  SECTION("nothing about an unrevealed opponent leaks") {
    BattleState other = s;
    other.teams[1][1] = make_combatant(find_species_id("Snorlax"));
    other.teams[1][1].currentHp = 1;
    REQUIRE(observe(other, 0) == view);
  }
}

TEST_CASE("observe hides the remaining turns of sleep, except after Rest", "[observe]") {
  ensureInit();
  BattleState s = battle({"Snorlax"}, {"Blissey"});
  start_battle(s, 1);
  for (int side = 0; side < 2; ++side) {
    s.teams[static_cast<size_t>(side)][0].status = Status::Sleep;
    s.teams[static_cast<size_t>(side)][0].status_turns = 2;
  }

  SECTION("on both sides: even a player does not know its own counter") {
    const BattleState view = observe(s, 0);
    REQUIRE(view.teams[0][0].status == Status::Sleep);
    REQUIRE(view.teams[0][0].status_turns == 0);
    REQUIRE(view.teams[1][0].status_turns == 0);
  }

  SECTION("Rest always sleeps two turns: its counter is public") {
    s.teams[1][0].sleep_self_inflicted = 1;
    REQUIRE(observe(s, 0).teams[1][0].status_turns == 2);
  }

  SECTION("Toxic's counter shows in the damage, so it stays") {
    s.teams[1][0].status = Status::Toxic;
    s.teams[1][0].status_turns = 3;
    REQUIRE(observe(s, 0).teams[1][0].status_turns == 3);
  }
}
