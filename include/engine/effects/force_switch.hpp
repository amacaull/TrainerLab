#pragma once

#include "engine/effects/effect.hpp"

namespace engine {

// Whirlwind / Dragon Tail: drags a RANDOM healthy benched opponent in.
// Empty bench: Whirlwind fails, Dragon Tail stays damage-only (canon).
// The dragged-in Pokemon takes entry hazards and fires its ability.
class ForceSwitchEffect : public Effect {
public:
  void apply(EffectContext &ctx) const override;
  const char *name() const override { return "ForceSwitch"; }
};

} // namespace engine
