#include "engine/effects/set_weather.hpp"

#include "engine/battle_state.hpp"
#include "engine/move.hpp"

namespace engine {

void SetWeatherEffect::apply(EffectContext &ctx) const {
  if (ctx.state.weather == weather_) {
    ctx.events.emplace_back(MoveFailedEvent{ctx.user, ctx.move.name});
    ctx.moveFailed = true;
    return;
  }
  ctx.state.weather = weather_;
  ctx.state.weather_turns_left = kWeatherDuration;
  ctx.events.emplace_back(WeatherStartedEvent{weather_});
}

} // namespace engine
