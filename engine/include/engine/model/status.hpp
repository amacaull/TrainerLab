#pragma once

#include <string_view>

namespace engine {
// Crosses the FFI as int: do not reorder.
enum class Status : int { None = 0, Burn, Poison, Toxic, Paralysis, Sleep, Freeze, Count };

constexpr int StatusCount = static_cast<int>(Status::Count);

std::string_view statusName(Status s);
Status statusFromString(std::string_view s);

struct Species;
bool typeImmuneToStatus(Status s, const Species &sp);

// Probabilities go through chance(), accuracy through chancePct(): FixedRNG can force a proc
// without touching accuracy.
constexpr float kFullParalysisChance = 0.25f;
constexpr float kThawChance = 0.20f;
constexpr int kBurnDamageDenom = 16;
constexpr int kPoisonDamageDenom = 8;
constexpr int kToxicDamageDenom = 16;
constexpr int kSleepMinTurns = 1;
constexpr int kSleepMaxTurns = 3;
} // namespace engine
