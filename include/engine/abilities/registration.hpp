#pragma once

#include "engine/abilities/ability.hpp"

#include <vector>

namespace engine {

// One register function per family file; ability.cpp calls them in a fixed
// order to build the registry (no static-init-order tricks).
//
// APPEND-ONLY. The position of an ability in this table is its id, and that
// id crosses the FFI inside AbilityTriggeredEvent / AbilityDamageEvent. Never
// reorder a family, never reorder the calls, never insert in the middle: add
// at the end of the matching family, which is also the end of the table.
using AbilityTable = std::vector<const Ability *>;

void registerDamageModAbilities(AbilityTable &table);
void registerImmunityAbilities(AbilityTable &table);
void registerWeatherAbilities(AbilityTable &table);
void registerSwitchHookAbilities(AbilityTable &table);
void registerTriggerAbilities(AbilityTable &table);

} // namespace engine
