#pragma once

#include "engine/effect.hpp"
#include "engine/field.hpp"

namespace engine {

// Rain Dance, Sunny Day, Sandstorm, Hail. Fails if the same weather is
// already active; a different weather is replaced (canon).
class SetWeatherEffect : public Effect {
public:
  explicit SetWeatherEffect(Weather weather) : weather_(weather) {}
  void apply(EffectContext &ctx) const override;
  const char *name() const override { return "SetWeather"; }

private:
  Weather weather_;
};

} // namespace engine
