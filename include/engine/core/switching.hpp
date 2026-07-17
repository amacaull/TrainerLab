#pragma once

#include "engine/core/events.hpp"

namespace engine {

struct BattleState;
class DataLoader;

// Shared by the engine (Switch action, KO replacements, battle start) and
// PivotEffect. Resets the outgoing Pokemon's stat stages and Toxic counter,
// updates activeIndex, then fires the incoming Pokemon's on_switch_in ability.
void performSwitch(BattleState &state, const DataLoader &data, int side, int newIndex,
                   EventLog &events);

// True if teamIndex is a legal switch target: in range, not the current
// active, not empty, not fainted.
bool isValidSwitchTarget(const BattleState &state, int side, int teamIndex);

// First healthy benched teammate, or -1 if none (used by Pivot auto-target).
int firstHealthyBenched(const BattleState &state, int side);

// Grounded = not Flying-type and not Levitate. Shared by Spikes, Toxic
// Spikes and Electric Terrain (Stealth Rock hits everyone, canon).
bool isGrounded(const Species &sp);

} // namespace engine
