#include "engine/effects/set_hazard.hpp"

#include "engine/core/battle_state.hpp"
#include "engine/model/move.hpp"

namespace engine {
void SetHazardEffect::apply(EffectContext &ctx) const {
  int side = 1 - ctx.user.side;
  SideHazards &hz = ctx.state.hazards[static_cast<size_t>(side)];

  int *counter = nullptr;
  int cap = 0;
  switch (hazard_) {
  case HazardKind::StealthRock:
    counter = &hz.stealth_rock;
    cap = kMaxStealthRock;
    break;
  case HazardKind::Spikes:
    counter = &hz.spikes;
    cap = kMaxSpikes;
    break;
  case HazardKind::ToxicSpikes:
    counter = &hz.toxic_spikes;
    cap = kMaxToxicSpikes;
    break;
  default:
    return;
  }

  if (*counter >= cap) {
    ctx.events.emplace_back(MoveFailedEvent{ctx.user, ctx.move.name});
    ctx.moveFailed = true;
    return;
  }
  *counter += 1;
  ctx.events.emplace_back(HazardSetEvent{side, hazard_, *counter});
}
} // namespace engine
