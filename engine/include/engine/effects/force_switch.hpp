#pragma once

#include "engine/effects/effect.hpp"

namespace engine {
// Empty bench: Whirlwind fails, Dragon Tail stays damage-only (canon).
class ForceSwitchEffect : public Effect {
public:
  void apply(EffectContext &ctx) const override;
  const char *name() const override { return "ForceSwitch"; }
};
} // namespace engine
