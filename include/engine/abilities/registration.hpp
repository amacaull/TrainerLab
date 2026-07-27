#pragma once

#include "engine/abilities/ability.hpp"

#include <string>
#include <unordered_map>

namespace engine {

// One register function per family file; ability.cpp calls them to build
// the registry (no static-init-order tricks).
using AbilityMap = std::unordered_map<std::string, const Ability *>;

void registerDamageModAbilities(AbilityMap &map);
void registerImmunityAbilities(AbilityMap &map);
void registerWeatherAbilities(AbilityMap &map);
void registerSwitchHookAbilities(AbilityMap &map);
void registerTriggerAbilities(AbilityMap &map);

} // namespace engine
