#pragma once

#include "engine/effects/effect.hpp"
#include "engine/model/field.hpp"

namespace engine {
class SetWeatherEffect : public Effect {
public:
  explicit SetWeatherEffect(Weather weather) : weather_(weather) {}
  void apply(EffectContext &ctx) const override;
  const char *name() const override { return "SetWeather"; }

private:
  Weather weather_;
};
} // namespace engine
