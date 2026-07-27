#include "engine/effects/clear_hazards.hpp"

#include "engine/core/battle_state.hpp"

namespace engine {

void ClearHazardsEffect::apply(EffectContext &ctx) const {
  auto clearSide = [&ctx](int side) {
    SideHazards &hz = ctx.state.hazards[static_cast<size_t>(side)];
    if (hz.stealth_rock == 0 && hz.spikes == 0 && hz.toxic_spikes == 0)
      return;
    hz = {};
    ctx.events.emplace_back(HazardsClearedEvent{side});
  };

  clearSide(ctx.user.side);
  if (bothSides_)
    clearSide(1 - ctx.user.side);

  if (clearScreens_) {
    for (int side = 0; side < kSideCount; ++side) {
      if (ctx.state.aurora_veil_turns[static_cast<size_t>(side)] > 0) {
        ctx.state.aurora_veil_turns[static_cast<size_t>(side)] = 0;
        ctx.events.emplace_back(ScreenEndedEvent{side});
      }
    }
  }
}

} // namespace engine
