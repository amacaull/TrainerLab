#pragma once

#include <string>

namespace engine {

// StrongWinds (Souffle Delta) is presence-bound: no countdown, normal
// setters fail against it, cleared when its holder leaves (ADR #47).
//
// Crosses the FFI boundary as int inside FfiEvent. Do not reorder.
enum class Weather : int { None = 0, Rain, Sun, Sand, Snow, StrongWinds, Count };

// One terrain in the whole roster (ADR #38): no speculative generalization.
// Crosses the FFI boundary as int inside FfiEvent. Do not reorder.
enum class Terrain : int { None = 0, Electric, Count };

// Fixed duration; no held items in scope (Damp Rock etc. would push to 8).
constexpr int kWeatherDuration = 5;
constexpr int kWeatherChipDenom = 16; // Sand residual: 1/16 max HP (snow does not chip)

// Crosses the FFI boundary as int inside FfiEvent. Do not reorder.
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
