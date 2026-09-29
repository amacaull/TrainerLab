#include "engine/effects/secondary.hpp"

#include "engine/core/rng.hpp"

namespace engine {
void SecondaryEffect::apply(EffectContext &ctx) const {
  if (!ctx.rng.chance(probability_))
    return;
  inner_->apply(ctx);
}
} // namespace engine
