#pragma once

#include "engine/effects/effect.hpp"

namespace engine {

// Voile Aurore (ADR #39): only under snow, one screen per side (re-set
// fails), physical AND special damage halved, ignored by crits. Duration
// is fixed at set time: 5 turns, or the setter's item say (Lumargile: 8).
class SetScreenEffect : public Effect {
public:
  void apply(EffectContext &ctx) const override;
  const char *name() const override { return "SetScreen"; }
};

} // namespace engine
