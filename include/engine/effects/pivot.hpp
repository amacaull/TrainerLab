#pragma once

#include "engine/effect.hpp"

namespace engine {

// U-Turn / Volt Switch: placed after Damage in the move's effect list.
// Switches the user to ctx.pivotTarget (declared in the UseMove action,
// ADR #20), falling back to the first healthy benched teammate; if the
// bench is empty the move is damage-only (canon).
class PivotEffect : public Effect {
public:
  void apply(EffectContext &ctx) const override;
  const char *name() const override { return "Pivot"; }
};

} // namespace engine
