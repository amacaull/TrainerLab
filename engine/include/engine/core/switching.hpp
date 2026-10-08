#pragma once

#include "engine/core/events.hpp"

namespace engine {
struct BattleState;
class DataLoader;

void performSwitch(BattleState &state, const DataLoader &data, int side, int newIndex,
                   EventLog &events);

bool isValidSwitchTarget(const BattleState &state, int side, int teamIndex);

int firstHealthyBenched(const BattleState &state, int side);

// Stealth Rock hits everyone (canon); only Spikes, Toxic Spikes and Electric Terrain care about
// grounding.
bool isGrounded(const Species &sp);
} // namespace engine
