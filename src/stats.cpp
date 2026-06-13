#include "engine/stats.hpp"

#include <algorithm>

namespace engine {

Stats computeStats(const Stats &base, int level) {
  auto otherStat = [level](int b) { return (2 * b * level) / 100 + 5; };

  Stats s;
  s.hp = (2 * base.hp * level) / 100 + level + 10;
  s.atk = otherStat(base.atk);
  s.def = otherStat(base.def);
  s.specAtk = otherStat(base.specAtk);
  s.specDef = otherStat(base.specDef);
  s.speed = otherStat(base.speed);
  return s;
}

float stageMultiplier(int stage) {
  stage = std::clamp(stage, -6, 6);
  if (stage >= 0)
    return static_cast<float>(2 + stage) / 2.0f;
  return 2.0f / static_cast<float>(2 - stage);
}

} // namespace engine
