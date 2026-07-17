#pragma once

#include <string>

namespace engine {

enum class Weather : int { None = 0, Rain, Sun, Sand, Snow, Count };

// One terrain in the whole roster (ADR #38): no speculative generalization.
enum class Terrain : int { None = 0, Electric, Count };

// Fixed duration; no held items in scope (Damp Rock etc. would push to 8).
constexpr int kWeatherDuration = 5;
constexpr int kWeatherChipDenom = 16; // Sand residual: 1/16 max HP (snow does not chip)

enum class HazardKind : int { StealthRock = 0, Spikes, ToxicSpikes, Count };

// Canon layer caps.
constexpr int kMaxStealthRock = 1;
constexpr int kMaxSpikes = 3;
constexpr int kMaxToxicSpikes = 2;

// Per-side entry hazards. POD on purpose: lives inside BattleState (FFI).
struct SideHazards {
  int stealth_rock = 0; // 0..1
  int spikes = 0;       // 0..3
  int toxic_spikes = 0; // 0..2
};

const char *weatherToString(Weather w);
Weather weatherFromString(const std::string &s);
const char *hazardToString(HazardKind h);
HazardKind hazardFromString(const std::string &s);

} // namespace engine
