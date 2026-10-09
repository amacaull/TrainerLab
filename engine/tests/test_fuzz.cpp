// Random battles through the FFI surface, checked against invariants rather
// than expected values. Scale with FUZZ_BATTLES=<n>; FUZZ_SEED=<s> replays.
#include "engine/ffi/ffi.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cstdlib>
#include <map>
#include <random>
#include <sstream>
#include <string>
#include <vector>

using namespace engine;
using namespace engine::ffi;

namespace {
constexpr int kTeam = 3;
constexpr int kMaxTurns = 1000;
constexpr int kMaxKind = 35;

uint64_t envOr(const char *name, uint64_t fallback) {
  const char *v = std::getenv(name);
  return v ? std::strtoull(v, nullptr, 10) : fallback;
}

std::string speciesOf(const BattleState &s, int side, int slot) {
  return species_id_string(s.teams[size_t(side)][size_t(slot)].species_id);
}

std::string moveLabel(int id) {
  if (id == kFfiStruggle)
    return "Struggle";
  return id >= 0 && id < move_count() ? move_name(id) : std::to_string(id);
}

std::string describe(const std::vector<FfiEvent> &ev) {
  std::ostringstream o;
  for (const FfiEvent &e : ev) {
    o << int(e.kind) << "(" << int(e.side) << "," << int(e.slot);
    if (e.kind == 0 || e.kind == 3 || e.kind == 14 || e.kind == 24)
      o << "," << moveLabel(e.name_id);
    else if (e.name_id != kFfiNoName)
      o << ",#" << e.name_id;
    if (e.i0 != 0)
      o << ",i0=" << e.i0;
    o << ") ";
  }
  return o.str();
}

std::string checkEvent(const FfiEvent &e, const BattleState &s) {
  if (e.kind > kMaxKind)
    return "unknown kind " + std::to_string(e.kind);
  if (e.side < -1 || e.side > 1)
    return "side out of range";
  if (e.side >= 0 && (e.slot < -1 || e.slot >= s.team_size[size_t(e.side)]))
    return "slot out of range";
  const bool sideScoped = e.kind == 18 || e.kind == 20 || e.kind == 32 || e.kind == 33;
  if (e.side >= 0 && e.slot == -1 && !sideScoped)
    return "kind " + std::to_string(e.kind) + " without a slot";
  if (e.name_id == kFfiNoName)
    return {};
  const bool moveKind = e.kind == 0 || e.kind == 3 || e.kind == 14 || e.kind == 24;
  const bool abilityKind = e.kind == 13 || e.kind == 35;
  const bool itemKind = e.kind >= 26 && e.kind <= 29;
  if (moveKind && (e.name_id == kFfiStruggle || e.name_id < move_count()) && e.name_id >= -2)
    return {};
  if (abilityKind && (e.name_id == kFfiStruggleRecoil || (e.name_id >= 0 && e.name_id < ability_count())))
    return {};
  if (itemKind && e.name_id >= 0 && e.name_id < item_count())
    return {};
  return "kind " + std::to_string(e.kind) + " has name_id " + std::to_string(e.name_id);
}

// What the frontend must rebuild from the events alone between SYNC_STATEs:
// HP, status, PP and whether the held item is still there.
struct Mirror {
  int hp = 0;
  Status status = Status::None;
  std::array<int, kMaxMovesPerPokemon> pp{};
  bool hasItem = false;
};
using Ledger = std::array<std::array<Mirror, kTeamSize>, kSideCount>;

Mirror mirrorOf(const BattlePokemon &p) {
  return Mirror{p.currentHp, p.status, p.pp, p.item_id != kNoItem && !p.item_consumed};
}

Ledger ledgerOf(const BattleState &s) {
  Ledger l{};
  for (int side = 0; side < kSideCount; ++side)
    for (int i = 0; i < s.team_size[size_t(side)]; ++i)
      l[size_t(side)][size_t(i)] = mirrorOf(s.teams[size_t(side)][size_t(i)]);
  return l;
}

void replay(Ledger &l, const BattleState &s, const std::vector<FfiEvent> &ev) {
  const FfiEvent *prev = nullptr;
  for (const FfiEvent &e : ev) {
    if (e.side < 0 || e.slot < 0) {
      prev = &e;
      continue;
    }
    Mirror &m = l[size_t(e.side)][size_t(e.slot)];
    const BattlePokemon &p = s.teams[size_t(e.side)][size_t(e.slot)];
    const int foeSide = 1 - e.side;
    switch (e.kind) {
    case 0:
    case 24: // MoveUsed / Charging carry the PP actually spent in i0
      for (size_t k = 0; k < p.move_ids.size(); ++k)
        if (e.i0 > 0 && p.move_ids[k] == e.name_id) {
          m.pp[k] -= e.i0;
          break;
        }
      break;
    case 1: case 6: case 17: case 19: case 23: case 28: case 35:
      m.hp = std::max(0, m.hp - e.i0);
      break;
    case 22:
      m.hp = std::min(p.stats.hp, m.hp + e.i0);
      break;
    case 2:
      m.hp = 0;
      break;
    case 4:
      m.status = static_cast<Status>(e.i0);
      break;
    case 7:
      m.status = Status::None;
      break;
    case 27:
      m.hasItem = false;
      break;
    case 29:
      m.hasItem = false;
      // Magician: the thief is announced just before the victim loses it.
      if (prev && prev->kind == 13 && prev->side == foeSide && prev->slot >= 0 &&
          ability_name(prev->name_id) == "Magician")
        l[size_t(foeSide)][size_t(prev->slot)].hasItem = true;
      break;
    default:
      break;
    }
    prev = &e;
  }
}

struct Fuzzer {
  std::mt19937_64 rng;
  std::map<std::string, std::string> findings;

  explicit Fuzzer(uint64_t seed) : rng(seed) {}

  int pick(int n) { return int(std::uniform_int_distribution<int>(0, n - 1)(rng)); }

  void report(const std::string &signature, uint64_t battleSeed, int turn,
              const std::string &detail) {
    if (findings.count(signature))
      return;
    std::ostringstream o;
    o << "FUZZ_SEED=" << battleSeed << " turn " << turn << "\n      " << detail;
    findings[signature] = o.str();
  }

  bool buildTeams(BattleState &s) {
    std::vector<int> ids(static_cast<size_t>(species_count()));
    for (int i = 0; i < species_count(); ++i)
      ids[size_t(i)] = i;
    std::shuffle(ids.begin(), ids.end(), rng);
    for (int side = 0; side < kSideCount; ++side)
      for (int i = 0; i < kTeam; ++i)
        s.teams[size_t(side)][size_t(i)] = make_combatant(ids[size_t(side * kTeam + i)]);
    s.team_size = {kTeam, kTeam};
    try {
      validate_team(s.teams[0], kTeam);
      validate_team(s.teams[1], kTeam);
    } catch (const std::exception &) {
      return false;
    }
    return true;
  }

  FfiAction randomAction() {
    if (pick(100) < 15)
      return FfiAction{1, int32_t(pick(kTeam)), -1};
    return FfiAction{0, int32_t(pick(kMaxMovesPerPokemon)), int32_t(pick(kTeam + 1) - 1)};
  }

  template <typename F>
  bool step(BattleState &s, uint64_t seed, const char *what, F &&fn) {
    const Ledger before = ledgerOf(s);
    std::vector<FfiEvent> ev;
    try {
      ev = fn();
    } catch (const std::exception &e) {
      report(std::string(what) + " threw: " + e.what(), seed, s.turn, e.what());
      return false;
    }
    try {
      validate_state(s);
    } catch (const std::exception &e) {
      report(std::string("invalid state after ") + what + ": " + e.what(), seed, s.turn,
             describe(ev));
      return false;
    }
    for (const FfiEvent &e : ev) {
      const std::string bad = checkEvent(e, s);
      if (!bad.empty())
        report("event: " + bad, seed, s.turn, describe(ev));
    }
    Ledger l = before;
    replay(l, s, ev);
    for (int side = 0; side < kSideCount; ++side)
      for (int i = 0; i < kTeam; ++i) {
        const Mirror real = mirrorOf(s.teams[size_t(side)][size_t(i)]);
        const Mirror &got = l[size_t(side)][size_t(i)];
        const std::string who = speciesOf(s, side, i);
        auto flag = [&](const std::string &field, int realV, int replayedV) {
          if (realV == replayedV)
            return;
          std::ostringstream d;
          d << "real " << realV << " replayed " << replayedV << " | " << describe(ev);
          report(field + " not explained by events: " + who + (realV > replayedV ? " (+)" : " (-)"),
                 seed, s.turn, d.str());
        };
        flag("HP", real.hp, got.hp);
        flag("status", int(real.status), int(got.status));
        flag("item", real.hasItem, got.hasItem);
        for (size_t k = 0; k < real.pp.size(); ++k)
          flag("PP", real.pp[k], got.pp[k]);
      }
    return true;
  }

  bool replacements(BattleState &s, uint64_t seed) {
    std::vector<int> order{faster_side(s, seed), 1 - faster_side(s, seed)};
    for (int side : order) {
      if (!s.active(side).isFainted() || side_has_lost(s, side))
        continue;
      std::vector<int> bench;
      for (int i = 0; i < kTeam; ++i) {
        BattleState probe = s;
        try {
          resolve_replacement(probe, side, i);
        } catch (const std::exception &) {
          continue;
        }
        bench.push_back(i);
      }
      if (legal_replacements(s, side) != bench)
        report("legal_replacements differs from what resolve_replacement accepts", seed, s.turn,
               speciesOf(s, side, s.activeIndex[size_t(side)]));
      const int pickIdx = bench[size_t(pick(int(bench.size())))];
      if (!step(s, seed, "resolve_replacement",
                [&] { return resolve_replacement(s, side, pickIdx); }))
        return false;
    }
    return true;
  }

  // legal_actions must match resolve_turn exactly: every listed action is accepted, and every
  // accepted one is listed, bar the forced cases collapsed to one move entry (charging accepts
  // anything, Struggle accepts every slot).
  void checkLegality(const BattleState &s, uint64_t seed, uint64_t turnSeed) {
    const std::vector<FfiAction> legal[2] = {legal_actions(s, 0), legal_actions(s, 1)};
    for (int side = 0; side < kSideCount; ++side) {
      const auto &mine = legal[size_t(side)];
      const auto &theirs = legal[size_t(1 - side)];
      if (mine.empty() || theirs.empty()) {
        report("no legal action for a standing active", seed, s.turn, speciesOf(s, side, 0));
        return;
      }
      auto accepted = [&](const FfiAction &a) {
        BattleState probe = s;
        try {
          if (side == 0)
            resolve_turn(probe, a, theirs[0], turnSeed);
          else
            resolve_turn(probe, theirs[0], a, turnSeed);
        } catch (const std::exception &) {
          return false;
        }
        return true;
      };
      auto listed = [&](const FfiAction &a) {
        return std::any_of(mine.begin(), mine.end(), [&](const FfiAction &l) {
          return l.kind == a.kind && l.index == a.index;
        });
      };
      for (const FfiAction &a : mine)
        if (!accepted(a))
          report("legal_actions lists a refused action", seed, s.turn,
                 "kind " + std::to_string(a.kind) + " index " + std::to_string(a.index));

      const bool charging = s.active(side).charging_move_id != kNoMove;
      int acceptedMoves = 0;
      std::vector<FfiAction> grid;
      for (int m = 0; m < kMaxMovesPerPokemon; ++m)
        grid.push_back(FfiAction{0, m, -1});
      for (int i = 0; i < kTeam; ++i)
        grid.push_back(FfiAction{1, i, -1});
      std::vector<FfiAction> missing;
      for (const FfiAction &a : grid) {
        if (!accepted(a))
          continue;
        if (a.kind == 0)
          ++acceptedMoves;
        if (!listed(a))
          missing.push_back(a);
      }
      const bool struggling = acceptedMoves == kMaxMovesPerPokemon &&
                              std::count_if(mine.begin(), mine.end(),
                                            [](const FfiAction &a) { return a.kind == 0; }) == 1;
      for (const FfiAction &a : missing)
        if (!charging && !(struggling && a.kind == 0))
          report("legal_actions misses an accepted action", seed, s.turn,
                 "kind " + std::to_string(a.kind) + " index " + std::to_string(a.index));
    }
  }

  // Each active has been seen, and no unrevealed opponent shows through observe().
  void checkViews(const BattleState &s, uint64_t seed) {
    for (int side = 0; side < kSideCount; ++side) {
      if (s.active(side).revealed != 1)
        report("active not revealed", seed, s.turn,
               speciesOf(s, side, s.activeIndex[size_t(side)]));
      const BattleState view = observe(s, side);
      for (int i = 0; i < kTeam; ++i)
        if (s.teams[size_t(1 - side)][size_t(i)].revealed == 0 &&
            !(view.teams[size_t(1 - side)][size_t(i)] == BattlePokemon{}))
          report("observe leaks an unrevealed opponent", seed, s.turn, speciesOf(s, 1 - side, i));
    }
  }

  // A rejected pair must leave the state as it was.
  bool rejectedCleanly(const BattleState &before, const BattleState &after) {
    for (int side = 0; side < kSideCount; ++side)
      for (int i = 0; i < kTeam; ++i) {
        const BattlePokemon &a = before.teams[size_t(side)][size_t(i)];
        const BattlePokemon &b = after.teams[size_t(side)][size_t(i)];
        if (a.currentHp != b.currentHp || a.pp != b.pp || a.status != b.status ||
            a.stat_stages != b.stat_stages || a.item_id != b.item_id)
          return false;
      }
    return before.turn == after.turn && before.activeIndex == after.activeIndex;
  }

  void battle(uint64_t seed) {
    rng.seed(seed);
    BattleState s;
    while (!buildTeams(s))
      s = BattleState{};
    if (!step(s, seed, "start_battle", [&] { return start_battle(s, seed); }))
      return;

    while (!is_over(s)) {
      if (s.turn >= kMaxTurns) {
        report("battle exceeds " + std::to_string(kMaxTurns) + " turns", seed, s.turn,
               speciesOf(s, 0, s.activeIndex[0]) + " vs " + speciesOf(s, 1, s.activeIndex[1]));
        return;
      }
      if (s.active(0).isFainted() || s.active(1).isFainted()) {
        if (!replacements(s, seed ^ uint64_t(s.turn)))
          return;
        continue;
      }
      const uint64_t turnSeed = seed * 1000003u + uint64_t(s.turn);
      checkViews(s, seed);
      checkLegality(s, seed, turnSeed);
      bool played = false;
      for (int attempt = 0; attempt < 30 && !played; ++attempt) {
        const FfiAction a0 = randomAction(), a1 = randomAction();
        const BattleState before = s;
        try {
          resolve_turn(s, a0, a1, turnSeed);
        } catch (const std::exception &e) {
          if (std::string(e.what()).rfind("E_ACTION:", 0) != 0) {
            report(std::string("resolve_turn threw: ") + e.what(), seed, s.turn, e.what());
            return;
          }
          if (!rejectedCleanly(before, s))
            report("rejected action mutated the state", seed, s.turn, e.what());
          s = before;
          continue;
        }
        s = before; // replay the accepted pair through step() for the checks
        played = step(s, seed, "resolve_turn", [&] { return resolve_turn(s, a0, a1, turnSeed); });
        if (!played)
          return;
      }
      if (!played) {
        // Random draws kept missing: every slot and switch, paired exhaustively.
        std::vector<FfiAction> all;
        for (int m = 0; m < kMaxMovesPerPokemon; ++m)
          all.push_back(FfiAction{0, m, -1});
        for (int i = 0; i < kTeam; ++i)
          all.push_back(FfiAction{1, i, -1});
        for (const FfiAction &a0 : all) {
          for (const FfiAction &a1 : all) {
            BattleState probe = s;
            try {
              resolve_turn(probe, a0, a1, turnSeed);
            } catch (const std::exception &) {
              continue;
            }
            played = step(s, seed, "resolve_turn", [&] { return resolve_turn(s, a0, a1, turnSeed); });
            break;
          }
          if (played)
            break;
        }
        if (!played) {
          report("no legal action pair", seed, s.turn,
                 speciesOf(s, 0, s.activeIndex[0]) + " vs " + speciesOf(s, 1, s.activeIndex[1]));
          return;
        }
      }
    }
  }
};
} // namespace

TEST_CASE("random battles keep every invariant", "[fuzz]") {
  engine_init(BATTLE_ENGINE_DATA_DIR);
  const uint64_t only = envOr("FUZZ_SEED", 0);
  const uint64_t count = only ? 1 : envOr("FUZZ_BATTLES", 300);

  Fuzzer f(only);
  for (uint64_t i = 0; i < count; ++i)
    f.battle(only ? only : 0x5EED0000u + i);

  std::ostringstream o;
  for (const auto &[signature, repro] : f.findings)
    o << "\n  - " << signature << "\n    " << repro;
  INFO(f.findings.size() << " distinct finding(s):" << o.str());
  REQUIRE(f.findings.empty());
}
