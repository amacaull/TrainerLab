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

  bool operator==(const Stats &) const = default;
};

constexpr int kIv = 31;

struct Nature {
  int plus = -1;
  int minus = -1;
};

const Nature *natureByName(std::string_view name);

Stats computeStats(const Stats &base, int level, const Nature &nature, const Stats &evs);

float stageMultiplier(int stage);

float accuracyStageMultiplier(int stage);
} // namespace engine
