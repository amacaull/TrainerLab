#include "engine/stats.hpp"

#include <algorithm>
#include <string>
#include <unordered_map>

namespace engine {

namespace {

// 0=atk 1=def 2=specAtk 3=specDef 4=speed
const std::unordered_map<std::string, Nature> kNatures = {
    {"Hardi", {}},       {"Docile", {}},     {"Sérieux", {}},     {"Pudique", {}},
    {"Bizarre", {}},

    {"Solo", {0, 1}},    {"Brave", {0, 4}},  {"Rigide", {0, 2}},  {"Mauvais", {0, 3}},

    {"Assuré", {1, 0}},  {"Relax", {1, 4}},  {"Malin", {1, 2}},   {"Lâche", {1, 3}},

    {"Modeste", {2, 0}}, {"Doux", {2, 1}},   {"Discret", {2, 4}}, {"Foufou", {2, 3}},

    {"Calme", {3, 0}},   {"Gentil", {3, 1}}, {"Malpoli", {3, 4}}, {"Prudent", {3, 2}},

    {"Timide", {4, 0}},  {"Pressé", {4, 1}}, {"Jovial", {4, 2}},  {"Naïf", {4, 3}},
};

} // namespace

const Nature *natureByName(std::string_view name) {
  auto it = kNatures.find(std::string(name));
  return it == kNatures.end() ? nullptr : &it->second;
}

Stats computeStats(const Stats &base, int level, const Nature &nature, const Stats &evs) {
  auto core = [level](int b, int ev) { return ((2 * b + kIv + ev / 4) * level) / 100; };
  auto other = [&](int b, int ev, int statIdx) {
    int raw = core(b, ev) + 5;
    if (statIdx == nature.plus)
      return (raw * 110) / 100;
    if (statIdx == nature.minus)
      return (raw * 90) / 100;
    return raw;
  };

  Stats s;
  s.hp = core(base.hp, evs.hp) + level + 10;
  s.atk = other(base.atk, evs.atk, 0);
  s.def = other(base.def, evs.def, 1);
  s.specAtk = other(base.specAtk, evs.specAtk, 2);
  s.specDef = other(base.specDef, evs.specDef, 3);
  s.speed = other(base.speed, evs.speed, 4);
  return s;
}

float accuracyStageMultiplier(int stage) {
  stage = std::clamp(stage, -6, 6);
  if (stage >= 0)
    return static_cast<float>(3 + stage) / 3.0f;
  return 3.0f / static_cast<float>(3 - stage);
}

float stageMultiplier(int stage) {
  stage = std::clamp(stage, -6, 6);
  if (stage >= 0)
    return static_cast<float>(2 + stage) / 2.0f;
  return 2.0f / static_cast<float>(2 - stage);
}

} // namespace engine
