#include "engine/ffi/ffi.hpp"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <stdexcept>
#include <string>

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

// A 1v1 built the way Rust will: make_combatant only, never field by field.
BattleState duel(const char *left, const char *right) {
  BattleState s;
  s.teams[0][0] = make_combatant(find_species_id(left));
  s.teams[1][0] = make_combatant(find_species_id(right));
  s.team_size = {1, 1};
  return s;
}

} // namespace

TEST_CASE("make_combatant produces a complete, valid combatant", "[ffi][battle]") {
  ensureInit();
  const int id = find_species_id("Dragapult");
  REQUIRE(id >= 0);
  BattlePokemon p = make_combatant(id);

  SECTION("stats, HP and level come from the engine, never from the caller") {
    REQUIRE(p.species_id == id);
    REQUIRE(p.level == kBattleLevel);
    REQUIRE(p.stats.hp > 0);
    REQUIRE(p.currentHp == p.stats.hp);
  }

  SECTION("the four moves of the movepool are equipped with their PP") {
    for (size_t i = 0; i < static_cast<size_t>(kMaxMovesPerPokemon); ++i) {
      INFO("slot " << i);
      REQUIRE(p.move_ids[i] != kNoMove);
      REQUIRE(p.pp[i] > 0);
      REQUIRE(p.pp[i] == move_entry(p.move_ids[i]).pp);
    }
  }

  SECTION("an invalid species is a caller bug") {
    const std::string msg = messageOf([] { return make_combatant(9999); });
    INFO(msg);
    REQUIRE(startsWith(msg, "E_ARG:"));
  }

  SECTION("the result passes validation as-is") {
    BattleState s = duel("Dragapult", "Snorlax");
    REQUIRE_NOTHROW(validate_state(s));
    REQUIRE_NOTHROW(validate_team(s.teams[0], 1));
  }
}

TEST_CASE("catalog entries expose what the frontend needs", "[ffi][battle]") {
  ensureInit();
  SpeciesEntry sp = species_entry(find_species_id("Dragapult"));
  REQUIRE(sp.id_string == "Dragapult");
  REQUIRE_FALSE(sp.display_name.empty());
  REQUIRE(sp.weight_kg > 0.0);

  MoveEntry mv = move_entry(find_move_id("ShadowBall"));
  REQUIRE(mv.name == "ShadowBall");
  REQUIRE(mv.power > 0);
  REQUIRE(mv.pp > 0);

  REQUIRE(startsWith(messageOf([] { return species_entry(-1); }), "E_ARG:"));
  REQUIRE(startsWith(messageOf([] { return move_entry(move_count()); }), "E_ARG:"));
}

TEST_CASE("a battle runs end to end across the boundary", "[ffi][battle]") {
  ensureInit();
  BattleState s = duel("Dragapult", "Snorlax");

  SECTION("start_battle is not optional and yields events") {
    std::vector<FfiEvent> ev = start_battle(s, 42);
    REQUIRE_FALSE(is_over(s));
    REQUIRE_FALSE(side_has_lost(s, 0));
    // Every flattened event must carry a kind the contract table defines.
    for (const FfiEvent &e : ev)
      REQUIRE(e.kind <= 35);
  }

  SECTION("resolve_turn advances the turn counter and damages someone") {
    start_battle(s, 42);
    const int before = s.turn;
    std::vector<FfiEvent> ev = resolve_turn(s, FfiAction{0, 0, -1}, FfiAction{0, 0, -1}, 1);
    REQUIRE(s.turn == before + 1);
    REQUIRE_FALSE(ev.empty());
  }

  SECTION("the same seed replays identically, a different one need not") {
    BattleState a = duel("Dragapult", "Snorlax");
    BattleState b = duel("Dragapult", "Snorlax");
    start_battle(a, 7);
    start_battle(b, 7);
    resolve_turn(a, FfiAction{0, 0, -1}, FfiAction{0, 0, -1}, 99);
    resolve_turn(b, FfiAction{0, 0, -1}, FfiAction{0, 0, -1}, 99);
    REQUIRE(a.active(0).currentHp == b.active(0).currentHp);
    REQUIRE(a.active(1).currentHp == b.active(1).currentHp);
  }
}

TEST_CASE("a rejected turn leaves the caller's state untouched", "[ffi][battle]") {
  ensureInit();
  BattleState s = duel("Dragapult", "Snorlax");
  start_battle(s, 42);

  const int hp0 = s.active(0).currentHp;
  const int hp1 = s.active(1).currentHp;
  const int turn = s.turn;
  const int pp0 = s.active(0).pp[0];

  SECTION("an unknown action kind never reaches the engine") {
    const std::string msg =
        messageOf([&] { return resolve_turn(s, FfiAction{9, 0, -1}, FfiAction{0, 0, -1}, 1); });
    INFO(msg);
    REQUIRE(startsWith(msg, "E_ACTION:"));
  }

  SECTION("an illegal move slot is rejected before any mutation") {
    const std::string msg =
        messageOf([&] { return resolve_turn(s, FfiAction{0, 3, -1}, FfiAction{1, 5, -1}, 1); });
    INFO(msg);
    REQUIRE(startsWith(msg, "E_ACTION:"));
  }

  // D8: the boundary works on a copy and commits only on success, so a throw
  // must not leave a half-written turn behind.
  REQUIRE(s.turn == turn);
  REQUIRE(s.active(0).currentHp == hp0);
  REQUIRE(s.active(1).currentHp == hp1);
  REQUIRE(s.active(0).pp[0] == pp0);
}

TEST_CASE("faster_side answers the simultaneous-replacement question", "[ffi][battle]") {
  ensureInit();
  BattleState s = duel("Dragapult", "Snorlax");
  const int first = faster_side(s, 3);
  REQUIRE((first == 0 || first == 1));
  // Dragapult outruns Snorlax by a wide margin: no tie, so no coin flip.
  REQUIRE(first == 0);
  REQUIRE(faster_side(s, 999) == 0);
}

TEST_CASE("the boundary refuses to work over an invalid state", "[ffi][battle]") {
  ensureInit();
  BattleState s = duel("Dragapult", "Snorlax");
  s.teams[0][0].currentHp = -5;

  const std::string msg = messageOf([&] { return validate_state(s); });
  INFO(msg);
  REQUIRE(startsWith(msg, "E_STATE:"));
  REQUIRE(startsWith(messageOf([&] { return start_battle(s, 1); }), "E_STATE:"));
}

TEST_CASE("every error crossing the boundary carries a known prefix", "[ffi][battle]") {
  ensureInit();
  // A prefix is a convention nothing compiles (README.md section 6, errors);
  // this is the test that turns it into an invariant.
  BattleState bad = duel("Dragapult", "Snorlax");
  bad.teams[0][0].currentHp = -1;

  const std::string messages[] = {
      messageOf([] { return engine_init("/nope"); }),
      messageOf([] { return species_entry(-1); }),
      messageOf([] { return make_combatant(-1); }),
      messageOf([&] { return validate_state(bad); }),
      messageOf([&] { return start_battle(bad, 1); }),
      messageOf([] { return side_has_lost(duel("Dragapult", "Snorlax"), 7); }),
  };

  const char *known[] = {"E_INIT:", "E_DATA:", "E_ARG:", "E_STATE:", "E_TEAM:", "E_ACTION:"};
  for (const std::string &m : messages) {
    INFO(m);
    REQUIRE_FALSE(m.empty());
    bool matched = false;
    for (const char *p : known)
      matched = matched || startsWith(m, p);
    REQUIRE(matched);
  }
}

TEST_CASE("every action refusal carries a frozen subcode", "[ffi][battle]") {
  ensureInit();
  // E_ACTION is the only prefix that reaches a player, so its subcode is a
  // contract (README.md section 6, errors). A convention nothing compiles is a
  // convention that drifts; this test is the lock.
  BattleState s = duel("Dragapult", "Snorlax");
  start_battle(s, 42);

  BattleState fainted = duel("Dragapult", "Snorlax");
  fainted.teams[0][0].currentHp = 0;

  const std::pair<const char *, std::string> cases[] = {
      {"E_ACTION:BAD_KIND:",
       messageOf([&] { return resolve_turn(s, FfiAction{9, 0, -1}, FfiAction{0, 0, -1}, 1); })},
      {"E_ACTION:BAD_SLOT:",
       messageOf([&] { return resolve_turn(s, FfiAction{0, 7, -1}, FfiAction{0, 0, -1}, 1); })},
      {"E_ACTION:INVALID_SWITCH:",
       messageOf([&] { return resolve_turn(s, FfiAction{1, 5, -1}, FfiAction{0, 0, -1}, 1); })},
      {"E_ACTION:FAINTED:",
       messageOf([&] { return resolve_turn(fainted, FfiAction{0, 0, -1}, FfiAction{0, 0, -1}, 1); })},
      {"E_ACTION:NOT_FAINTED:", messageOf([&] { return resolve_replacement(s, 0, 1); })},
      {"E_ACTION:BAD_SIDE:", messageOf([&] { return resolve_replacement(s, 7, 0); })},
  };

  for (const auto &[expected, actual] : cases) {
    INFO(actual);
    REQUIRE(startsWith(actual, expected));
  }

  // No space after the prefix: Rust splits on the first two ':'.
  const std::string msg = cases[0].second;
  REQUIRE(msg.find("E_ACTION: ") == std::string::npos);
}

TEST_CASE("MoveUsed and Charging carry the PP actually spent in i0", "[ffi][battle]") {
  ensureInit();
  auto first = [](const std::vector<FfiEvent> &ev, uint8_t kind, int side) {
    for (const FfiEvent &e : ev)
      if (e.kind == kind && e.side == side)
        return e;
    FAIL("no event of kind " << int(kind) << " for side " << side);
    return FfiEvent{};
  };

  SECTION("a move aimed at a Pressure holder costs two") {
    BattleState s = duel("Dragapult", "Corviknight");
    start_battle(s, 1);
    const int before = s.active(0).pp[0];
    auto ev = resolve_turn(s, FfiAction{0, 0, -1}, FfiAction{0, 2, -1}, 1);
    const FfiEvent used = first(ev, 0, 0);
    REQUIRE(used.i0 == 2);
    REQUIRE(before - s.active(0).pp[0] == used.i0);
  }

  SECTION("a two-turn move pays on its charge turn, the release is free") {
    BattleState s = duel("Dragapult", "Snorlax");
    start_battle(s, 1);
    const int before = s.active(0).pp[1];
    auto charge = resolve_turn(s, FfiAction{0, 1, -1}, FfiAction{0, 0, -1}, 1);
    REQUIRE(first(charge, 24, 0).i0 == 1);
    auto release = resolve_turn(s, FfiAction{0, 1, -1}, FfiAction{0, 0, -1}, 2);
    REQUIRE(first(release, 0, 0).i0 == 0);
    REQUIRE(before - s.active(0).pp[1] == 1);
  }
}
