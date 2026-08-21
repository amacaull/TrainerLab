#include "engine/abilities/registration.hpp"

#include "engine/core/battle_state.hpp"

namespace engine {

namespace {

// Snow setter: the shared WeatherAbility pattern lives in ability.cpp with
// the legacy setters; this one adds the "fails under Souffle Delta" guard
// that every setter now needs, so all of them route through here eventually.
class SnowWarning final : public Ability {
public:
  const char *name() const override { return "SnowWarning"; }
  void onSwitchIn(AbilityContext &ctx) const override {
    if (ctx.state.weather == Weather::Snow || ctx.state.weather == Weather::StrongWinds)
      return;
    ctx.events.emplace_back(AbilityTriggeredEvent{ctx.self, name()});
    ctx.state.weather = Weather::Snow;
    ctx.state.weather_turns_left = kWeatherDuration;
    ctx.events.emplace_back(WeatherStartedEvent{Weather::Snow});
  }
};

class ElectricSurge final : public Ability {
public:
  const char *name() const override { return "ElectricSurge"; }
  void onSwitchIn(AbilityContext &ctx) const override {
    if (ctx.state.terrain == Terrain::Electric)
      return;
    ctx.events.emplace_back(AbilityTriggeredEvent{ctx.self, name()});
    ctx.state.terrain = Terrain::Electric;
    ctx.state.terrain_turns_left = kWeatherDuration;
    ctx.events.emplace_back(TerrainStartedEvent{Terrain::Electric});
  }
};

// Souffle Delta (ADR #47): presence-bound weather. No countdown; normal
// setters fail against it; it clears when the holder leaves the field
// (handled in performSwitch, faint included).
class DeltaStream final : public Ability {
public:
  const char *name() const override { return "DeltaStream"; }
  void onSwitchIn(AbilityContext &ctx) const override {
    if (ctx.state.weather == Weather::StrongWinds)
      return;
    ctx.events.emplace_back(AbilityTriggeredEvent{ctx.self, name()});
    ctx.state.weather = Weather::StrongWinds;
    ctx.state.weather_turns_left = 0;
    ctx.events.emplace_back(WeatherStartedEvent{Weather::StrongWinds});
  }
};

class WeatherSpeed : public Ability {
public:
  WeatherSpeed(const char *abilityName, Weather weather) : name_(abilityName), weather_(weather) {}
  const char *name() const override { return name_; }
  float speedMultiplier(const BattleState &state) const override {
    return state.weather == weather_ ? 2.0f : 1.0f;
  }

private:
  const char *name_;
  Weather weather_;
};

class LeafGuard final : public Ability {
public:
  const char *name() const override { return "LeafGuard"; }
  bool blocksStatus(const BattleState &state) const override {
    return state.weather == Weather::Sun;
  }
};

} // namespace

void registerWeatherAbilities(AbilityTable &table) {
  static const SnowWarning alerteNeige;
  static const ElectricSurge creaElec;
  static const DeltaStream souffleDelta;
  static const WeatherSpeed glissade{"SwiftSwim", Weather::Rain};
  static const WeatherSpeed baigneSable{"SandRush", Weather::Sand};
  static const WeatherSpeed chasseNeige{"SlushRush", Weather::Snow};
  static const LeafGuard feuilleGarde;
  auto add = [&table](const Ability &a) { table.push_back(&a); };
  add(alerteNeige);
  add(creaElec);
  add(souffleDelta);
  add(glissade);
  add(baigneSable);
  add(chasseNeige);
  add(feuilleGarde);
}

} // namespace engine
