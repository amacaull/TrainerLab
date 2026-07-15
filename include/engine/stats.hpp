#pragma once

namespace engine {

struct Stats {
  int hp = 0;
  int atk = 0;
  int def = 0;
  int specAtk = 0;
  int specDef = 0;
  int speed = 0;
};

// Modern-gen formula, IVs=0, EVs=0, neutral nature.
Stats computeStats(const Stats &base, int level);

// Canon stage multiplier for Atk/Def/SpA/SpD/Spe: (2+n)/2 for n>=0,
// 2/(2-n) for n<0. Stage is clamped to [-6, +6].
float stageMultiplier(int stage);

// Accuracy/Evasion table (distinct from the main one): (3+n)/3 for n>=0,
// 3/(3-n) for n<0. Applied to the accuracy roll as a single combined stage
// (user Acc - target Eva), clamped to [-6, +6].
float accuracyStageMultiplier(int stage);

} // namespace engine
