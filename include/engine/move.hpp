#pragma once

#include "engine/effect.hpp"
#include "engine/types.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace engine {

enum class MoveCategory { Physical, Special, Status };

// Two-turn moves; Fly and Dig grant semi-invulnerability during the charge.
enum class TwoTurn : int { None = 0, Charge, Fly, Dig };

MoveCategory categoryFromString(std::string_view s);
std::string_view categoryName(MoveCategory c);

struct Move {
  std::string name;
  Type type = Type::Normal;
  MoveCategory category = MoveCategory::Physical;
  int power = 0;
  int accuracy = 100;
  int priority = 0;             // -7..+5
  int pp = 0;                   // base PP = max PP (no PP Ups; ADR #35)
  bool makesContact = false;    // triggers Static / Rough Skin
  bool highCrit = false;        // +1 crit stage (Stone Edge...)
  bool bypassesProtect = false; // Whirlwind / Roar
  bool hitsDig = false;         // Earthquake: hits (and doubles on) Dig
  bool solarCharge = false;     // SolarBeam: no charge in sun, halved in bad weather
  bool blockedByProtect = true; // false for self/field moves (hazards, weather, recovery)
  bool typeless = false;        // Lutte only: x1 vs everything, never STAB
  TwoTurn twoTurn = TwoTurn::None;
  std::vector<EffectPtr> effects;

  Move() = default;
  Move(const Move &) = delete;
  Move &operator=(const Move &) = delete;
  Move(Move &&) = default;
  Move &operator=(Move &&) = default;
};

} // namespace engine
