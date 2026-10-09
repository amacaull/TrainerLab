#pragma once

#include <string>

namespace engine {
// StrongWinds is presence-bound: no countdown, and it ends when its holder leaves. Crosses the FFI
// as int: do not reorder.
enum class Weather : int { None = 0, Rain, Sun, Sand, Snow, StrongWinds, Count };

// Crosses the FFI as int: do not reorder.
enum class Terrain : int { None = 0, Electric, Count };

constexpr int kWeatherDuration = 5;
constexpr int kWeatherChipDenom = 16;

// Crosses the FFI as int: do not reorder.
enum class HazardKind : int { StealthRock = 0, Spikes, ToxicSpikes, Count };

constexpr int kMaxStealthRock = 1;
constexpr int kMaxSpikes = 3;
constexpr int kMaxToxicSpikes = 2;

// POD on purpose: it lives inside BattleState, which crosses the FFI.
struct SideHazards {
  int stealth_rock = 0;
  int spikes = 0;
  int toxic_spikes = 0;

  bool operator==(const SideHazards &) const = default;
};

const char *weatherToString(Weather w);
Weather weatherFromString(const std::string &s);
const char *hazardToString(HazardKind h);
HazardKind hazardFromString(const std::string &s);
} // namespace engine
