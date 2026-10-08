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

enum class TwoTurn : int { None = 0, Charge, Fly, Dig, Disappear };

MoveCategory categoryFromString(std::string_view s);
std::string_view categoryName(MoveCategory c);

struct Move {
  std::string name;
  Type type = Type::Normal;
  MoveCategory category = MoveCategory::Physical;
  int power = 0;
  int accuracy = 100;
  int priority = 0;
  int pp = 0;
  StatIndex offenseStat = StatIndex::Count;
  StatIndex defenseStat = StatIndex::Count;
  bool useTargetOffense = false;
  bool boostedByTargetItem = false;
  bool firstTurnOnly = false;
  bool failsIfTargetNotAttacking = false;
  bool requiresTargetItem = false;
  bool usableWhileAsleep = false;
  bool thawsUser = false;
  bool hitsFly = false;
  bool powerFromTargetWeight = false;
  Type alwaysHitsIfUserType = Type::Count;
  int minHits = 0;
  int maxHits = 0;
  bool perHitAccuracy = false;
  std::vector<int> hitPowers;

  std::vector<std::pair<Weather, int>> accuracyInWeather;
  bool makesContact = false;
  bool highCrit = false;
  bool bypassesProtect = false;
  bool hitsDig = false;
  bool solarCharge = false;
  bool blockedByProtect = true;
  bool typeless = false;
  bool punch = false;
  bool slicing = false;
  bool bulletproof = false;
  bool reflectable = false;
  TwoTurn twoTurn = TwoTurn::None;
  std::vector<EffectPtr> effects;

  Move() = default;
  Move(const Move &) = delete;
  Move &operator=(const Move &) = delete;
  Move(Move &&) = default;
  Move &operator=(Move &&) = default;
};
} // namespace engine
