#pragma once

#include "engine/effects/effect.hpp"
#include "engine/model/field.hpp"
#include "engine/model/types.hpp"

#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace engine {

enum class MoveCategory { Physical, Special, Status };

// Two-turn moves; Fly and Dig grant semi-invulnerability during the charge.
enum class TwoTurn : int { None = 0, Charge, Fly, Dig, Disappear };

MoveCategory categoryFromString(std::string_view s);
std::string_view categoryName(MoveCategory c);

struct Move {
  std::string name;
  Type type = Type::Normal;
  MoveCategory category = MoveCategory::Physical;
  int power = 0;
  int accuracy = 100;
  int priority = 0; // -7..+5
  int pp = 0;       // base PP = max PP (no PP Ups; ADR #35)
  // --- Mechanic fields (all data-driven, default = canon-neutral) ---
  // Damage stat plumbing: category picks atk/spa vs def/spd unless overridden.
  StatIndex offenseStat = StatIndex::Count; // Count = default by category (BodyPress: Def)
  StatIndex defenseStat = StatIndex::Count; // Psyshock: special move vs physical Def
  bool useTargetOffense = false;            // FoulPlay: the target's Atk swings
  bool boostedByTargetItem = false;         // KnockOff: x1.5 if the target holds something
  // Usability gates, checked by the engine before anything rolls.
  bool firstTurnOnly = false;              // FakeOut / FirstImpression
  bool failsIfTargetNotAttacking = false;  // SuckerPunch
  bool requiresTargetItem = false;         // Poltergeist
  bool usableWhileAsleep = false;          // SleepTalk (bypasses the sleep skip)
  bool thawsUser = false;                  // Scald / FlareBlitz
  bool hitsFly = false;                    // Hurricane reaches airborne targets
  bool powerFromTargetWeight = false;      // LowKick/GrassKnot: canon weight tiers
  Type alwaysHitsIfUserType = Type::Count; // Toxic from a Poison-type never misses
  // Multi-hit: minHits==0 = single hit. Escalating powers (TripleAxel) in
  // hitPowers; perHitAccuracy retests each hit; LoadedDice raises the floor.
  int minHits = 0;
  int maxHits = 0;
  bool perHitAccuracy = false;
  std::vector<int> hitPowers;

  // Weather-dependent accuracy overrides (0 = never miss), e.g. Blizzard
  // under snow. Data only.
  std::vector<std::pair<Weather, int>> accuracyInWeather;
  bool makesContact = false;    // triggers Static / Rough Skin
  bool highCrit = false;        // +1 crit stage (Stone Edge...)
  bool bypassesProtect = false; // Whirlwind / Roar
  bool hitsDig = false;         // Earthquake: hits (and doubles on) Dig
  bool solarCharge = false;     // SolarBeam: no charge in sun, halved in bad weather
  bool blockedByProtect = true; // false for self/field moves (hazards, weather, recovery)
  bool typeless = false;        // Struggle only: x1 vs everything, never STAB
  bool punch = false;           // IronFist x1.2
  bool slicing = false;         // Sharpness x1.5
  bool bulletproof = false;     // voided by Bulletproof
  bool reflectable = false;     // bounced by MagicBounce (Showdown's flag)
  TwoTurn twoTurn = TwoTurn::None;
  std::vector<EffectPtr> effects;

  Move() = default;
  Move(const Move &) = delete;
  Move &operator=(const Move &) = delete;
  Move(Move &&) = default;
  Move &operator=(Move &&) = default;
};

} // namespace engine
