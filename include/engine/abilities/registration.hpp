#pragma once

#include "engine/abilities/ability.hpp"

#include <vector>

namespace engine {
// APPEND-ONLY: the position of an ability is its id, and that id crosses the FFI.
using AbilityTable = std::vector<const Ability *>;

void registerDamageModAbilities(AbilityTable &table);
void registerImmunityAbilities(AbilityTable &table);
void registerWeatherAbilities(AbilityTable &table);
void registerSwitchHookAbilities(AbilityTable &table);
void registerTriggerAbilities(AbilityTable &table);
} // namespace engine
