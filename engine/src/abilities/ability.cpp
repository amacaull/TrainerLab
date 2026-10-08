#include "engine/abilities/registration.hpp"

#include "engine/core/battle_state.hpp"
#include "engine/core/data_loader.hpp"
#include "engine/core/rng.hpp"
#include "engine/effects/stat_change.hpp"
#include "engine/model/field.hpp"
#include "engine/model/status.hpp"
#include "engine/model/types.hpp"

#include <algorithm>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

namespace engine {
namespace {
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
    applyOpposingStatDrop(ctx.state, ctx.data, foeRef, StatIndex::Atk, -1, ctx.events);
  }
};

class WeatherAbility : public Ability {
public:
  WeatherAbility(const char *name, Weather weather) : name_(name), weather_(weather) {}
  const char *name() const override { return name_; }

  void onSwitchIn(AbilityContext &ctx) const override {
    if (ctx.state.weather == weather_ || ctx.state.weather == Weather::StrongWinds)
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

class ThickFat : public Ability {
public:
  const char *name() const override { return "ThickFat"; }

  float incomingDamageMultiplier(const Move &move) const override {
    return (move.type == Type::Fire || move.type == Type::Ice) ? 0.5f : 1.0f;
  }
};

constexpr float kStaticChance = 0.30f;

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

namespace {
// Frozen order: the position is the ability id, which crosses the FFI. Append only.
const AbilityTable &abilityTable() {
  static const AbilityTable table = [] {
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

    AbilityTable t = {
        &blaze,     // 0
        &torrent,   // 1
        &overgrow,  // 2
        &swarm,     // 3
        &sandStream,// 4
        &drizzle,   // 5
        &levitate,  // 6
        &intimidate,// 7
        &static_,   // 8
        &thickFat,  // 9
        &guts,      // 10
        &roughSkin, // 11
    };
    registerDamageModAbilities(t);
    registerImmunityAbilities(t);
    registerWeatherAbilities(t);
    registerSwitchHookAbilities(t);
    registerTriggerAbilities(t);
    return t;
  }();
  return table;
}

const std::unordered_map<std::string, int> &abilityIndex() {
  static const std::unordered_map<std::string, int> index = [] {
    std::unordered_map<std::string, int> m;
    const AbilityTable &t = abilityTable();
    for (size_t i = 0; i < t.size(); ++i) {
      if (!m.emplace(t[i]->name(), static_cast<int>(i)).second)
        throw std::runtime_error(std::string("Duplicate ability name: ") + t[i]->name());
    }
    return m;
  }();
  return index;
}
} // namespace

const Ability *abilityByIndex(int id) {
  const AbilityTable &t = abilityTable();
  if (id < 0 || id >= static_cast<int>(t.size()))
    return nullptr;
  return t[static_cast<size_t>(id)];
}

int abilityCount() { return static_cast<int>(abilityTable().size()); }

int findAbilityIdByName(std::string_view name) {
  const auto &index = abilityIndex();
  auto it = index.find(std::string(name));
  return (it == index.end()) ? -1 : it->second;
}

const Ability *abilityByName(std::string_view name) {
  return abilityByIndex(findAbilityIdByName(name));
}

const Ability *abilityOf(const DataLoader &data, const BattlePokemon &p) {
  if (p.isEmpty())
    return nullptr;
  return abilityByName(data.speciesByIndex(p.species_id).ability);
}

void abilityHpCheck(BattleState &state, const DataLoader &data, const CombatantRef &who,
                    int hpBefore, bool fromDirectHit, EventLog &events) {
  BattlePokemon &p = state.teams[static_cast<size_t>(who.side)][static_cast<size_t>(who.teamIndex)];
  int half = p.stats.hp / 2;
  if (p.isFainted() || hpBefore <= half || p.currentHp > half)
    return;
  if (const Ability *ability = abilityOf(data, p)) {
    AbilityContext ctx{state, data, events, who};
    ability->onHalfHpCrossed(ctx, fromDirectHit);
  }
}
} // namespace engine
