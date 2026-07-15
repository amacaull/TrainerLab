#pragma once

#include <string_view>

namespace engine {

struct Stats {
  int hp = 0;
  int atk = 0;
  int def = 0;
  int specAtk = 0;
  int specDef = 0;
  int speed = 0;
};

// IVs are a flat 31 everywhere (ADR #33): sets are fixed, nobody tunes them.
constexpr int kIv = 31;

// A nature is +10% on one non-HP stat and -10% on another (ADR #33).
// Indices follow Stats order minus HP: 0=atk 1=def 2=specAtk 3=specDef
// 4=speed; -1/-1 = neutral nature.
struct Nature {
  int plus = -1;
  int minus = -1;
};

// Canon table of the 25 natures, French names (accented). Returns nullptr
// on unknown name; the DataLoader turns that into a load error.
const Nature *natureByName(std::string_view name);

// Full canon formula at IV 31 (ADR #33):
//   hp    = floor((2*base + 31 + floor(ev/4)) * level / 100) + level + 10
//   other = floor((floor((2*base + 31 + floor(ev/4)) * level / 100) + 5) * nature)
Stats computeStats(const Stats &base, int level, const Nature &nature, const Stats &evs);

// Canon stage multiplier for Atk/Def/SpA/SpD/Spe: (2+n)/2 for n>=0,
// 2/(2-n) for n<0. Stage is clamped to [-6, +6].
float stageMultiplier(int stage);

// Accuracy/Evasion table (distinct from the main one): (3+n)/3 for n>=0,
// 3/(3-n) for n<0. Applied to the accuracy roll as a single combined stage
// (user Acc - target Eva), clamped to [-6, +6].
float accuracyStageMultiplier(int stage);

} // namespace engine
