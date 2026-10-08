#pragma once

#include "engine/effects/effect.hpp"

namespace engine {
class RecoilEffect : public Effect {
public:
  explicit RecoilEffect(int denominator) : denominator_(denominator) {}
  void apply(EffectContext &ctx) const override;
  const char *name() const override { return "Recoil"; }

private:
  int denominator_;
};
} // namespace engine
