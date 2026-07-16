#pragma once

#include <string_view>

namespace engine {

// Stable ordering, crosses the FFI boundary as int. Do not reorder.
enum class Status : int { None = 0, Burn, Poison, Toxic, Paralysis, Sleep, Freeze, Count };

constexpr int StatusCount = static_cast<int>(Status::Count);

std::string_view statusName(Status s);
Status statusFromString(std::string_view s);

struct Species;
// Canon type immunities: Fire can't burn, Electric can't be paralyzed,
// Poison/Steel can't be poisoned. Shared by ApplyStatus, Toxic Spikes, Static.
bool typeImmuneToStatus(Status s, const Species &sp);

// Canon gen 6+ values. Probabilities go through RNG::chance(float) so
// FixedRNG can force/deny procs without touching accuracy rolls (chancePct).
constexpr float kFullParalysisChance = 0.25f;
constexpr float kThawChance = 0.20f;
constexpr int kBurnDamageDenom = 16;  // maxHp / 16 per turn
constexpr int kPoisonDamageDenom = 8; // maxHp / 8 per turn
constexpr int kToxicDamageDenom = 16; // maxHp * n / 16, n = status_turns
constexpr int kSleepMinTurns = 1;
constexpr int kSleepMaxTurns = 3;

} // namespace engine
