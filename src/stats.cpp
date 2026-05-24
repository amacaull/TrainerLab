#include "engine/stats.hpp"

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

} // namespace engine
