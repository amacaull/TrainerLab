#pragma once

#include "engine/effect.hpp"

namespace engine {

// Protect: blocks foe-targeting moves this turn. Consecutive uses succeed
// with probability 1/3^n (Showdown). The chain resets at end of any turn
// where Protect did not connect (ADR #30).
class ProtectEffect : public Effect {
public:
  void apply(EffectContext &ctx) const override;
  const char *name() const override { return "Protect"; }
};

} // namespace engine
