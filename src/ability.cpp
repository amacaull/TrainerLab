#include "engine/ability.hpp"

#include "engine/battle_state.hpp"
#include "engine/data_loader.hpp"
#include "engine/effects/stat_change.hpp"
#include "engine/field.hpp"
#include "engine/rng.hpp"
#include "engine/status.hpp"
#include "engine/types.hpp"

#include <algorithm>
#include <string>
#include <unordered_map>

namespace engine {

namespace {

// Blaze / Torrent / Overgrow: x1.5 on same-type moves when HP <= 1/3.
class PinchAbility : public Ability {
public:
  PinchAbility(const char *name, Type boosted) : name_(name), boosted_(boosted) {}
  const char *name() const override { return name_; }

  float damageMultiplier(const Move &move, const BattlePokemon &user) const override {
    if (move.type == boosted_ && user.currentHp * 3 <= user.stats.hp)
      return 1.5f;
    return 1.0f;
  }

private:
  const char *name_;
  Type boosted_;
};

class Levitate : public Ability {
public:
  const char *name() const override { return "Levitate"; }
  bool immuneToMove(const Move &move) const override { return move.type == Type::Ground; }
};

class Intimidate : public Ability {
public:
  const char *name() const override { return "Intimidate"; }

  void onSwitchIn(AbilityContext &ctx) const override {
    int foeSide = 1 - ctx.self.side;
    BattlePokemon &foe = ctx.state.active(foeSide);
    if (foe.isEmpty() || foe.isFainted())
      return;
    ctx.events.emplace_back(AbilityTriggeredEvent{ctx.self, name()});
    CombatantRef foeRef{foeSide, ctx.state.activeIndex[static_cast<size_t>(foeSide)]};
    applyStatStageDelta(foe, foeRef, StatIndex::Atk, -1, ctx.events);
  }
};

// Sand Stream / Drizzle: set the matching weather on switch-in (5 turns,
// gen 6+); silent no-op if that weather is already up (canon).
class WeatherAbility : public Ability {
public:
  WeatherAbility(const char *name, Weather weather) : name_(name), weather_(weather) {}
  const char *name() const override { return name_; }

  void onSwitchIn(AbilityContext &ctx) const override {
    if (ctx.state.weather == weather_)
      return;
    ctx.events.emplace_back(AbilityTriggeredEvent{ctx.self, name()});
    ctx.state.weather = weather_;
    ctx.state.weather_turns_left = kWeatherDuration;
    ctx.events.emplace_back(WeatherStartedEvent{weather_});
  }

private:
  const char *name_;
  Weather weather_;
};

// Guts: Atk x1.5 on physical moves while statused; burn halving ignored.
class Guts : public Ability {
public:
  const char *name() const override { return "Guts"; }

  float damageMultiplier(const Move &move, const BattlePokemon &user) const override {
    if (user.status != Status::None && move.category == MoveCategory::Physical)
      return 1.5f;
    return 1.0f;
  }

  bool ignoresBurnPenalty() const override { return true; }
};

// Thick Fat: halves incoming Fire and Ice damage.
class ThickFat : public Ability {
public:
  const char *name() const override { return "ThickFat"; }

  float incomingDamageMultiplier(const Move &move) const override {
    return (move.type == Type::Fire || move.type == Type::Ice) ? 0.5f : 1.0f;
  }
};

constexpr float kStaticChance = 0.30f;

// Static: 30% to paralyze on contact (type immunities respected).
class Static : public Ability {
public:
  const char *name() const override { return "Static"; }

  void onDamagingHit(EffectContext &ctx, CombatantRef self, CombatantRef attacker,
                     bool contact) const override {
    if (!contact || !ctx.rng.chance(kStaticChance))
      return;
    BattlePokemon &atk =
        ctx.state
            .teams[static_cast<size_t>(attacker.side)][static_cast<size_t>(attacker.teamIndex)];
    if (atk.isFainted() || atk.status != Status::None)
      return;
    const Species &atkSp = ctx.data.speciesByIndex(atk.species_id);
    if (typeImmuneToStatus(Status::Paralysis, atkSp))
      return;
    ctx.events.emplace_back(AbilityTriggeredEvent{self, name()});
    atk.status = Status::Paralysis;
    atk.status_turns = 0;
    ctx.events.emplace_back(StatusAppliedEvent{attacker, Status::Paralysis});
  }
};

// Rough Skin: attacker loses 1/8 max HP on contact (can KO, canon).
class RoughSkin : public Ability {
public:
  const char *name() const override { return "RoughSkin"; }

  void onDamagingHit(EffectContext &ctx, CombatantRef self, CombatantRef attacker,
                     bool contact) const override {
    if (!contact)
      return;
    BattlePokemon &atk =
        ctx.state
            .teams[static_cast<size_t>(attacker.side)][static_cast<size_t>(attacker.teamIndex)];
    if (atk.isFainted())
      return;
    ctx.events.emplace_back(AbilityTriggeredEvent{self, name()});
    int damage = std::max(1, atk.stats.hp / 8);
    atk.currentHp = std::max(0, atk.currentHp - damage);
    ctx.events.emplace_back(RecoilDamageEvent{attacker, damage});
    if (atk.isFainted())
      ctx.events.emplace_back(FaintedEvent{attacker});
  }
};

} // namespace

const Ability *abilityByName(std::string_view name) {
  static const PinchAbility blaze{"Blaze", Type::Fire};
  static const PinchAbility torrent{"Torrent", Type::Water};
  static const PinchAbility overgrow{"Overgrow", Type::Grass};
  static const PinchAbility swarm{"Swarm", Type::Bug};
  static const WeatherAbility sandStream{"SandStream", Weather::Sand};
  static const WeatherAbility drizzle{"Drizzle", Weather::Rain};
  static const Levitate levitate;
  static const Intimidate intimidate;
  static const Static static_;
  static const ThickFat thickFat;
  static const Guts guts;
  static const RoughSkin roughSkin;

  static const std::unordered_map<std::string_view, const Ability *> registry = {
      {"Blaze", &blaze},
      {"Torrent", &torrent},
      {"Overgrow", &overgrow},
      {"Swarm", &swarm},
      {"SandStream", &sandStream},
      {"Drizzle", &drizzle},
      {"Levitate", &levitate},
      {"Intimidate", &intimidate},
      {"Static", &static_},
      {"ThickFat", &thickFat},
      {"Guts", &guts},
      {"RoughSkin", &roughSkin},
  };

  auto it = registry.find(name);
  return (it == registry.end()) ? nullptr : it->second;
}

} // namespace engine
