#include "engine/abilities/registration.hpp"

#include "engine/core/battle_state.hpp"
#include "engine/effects/stat_change.hpp"

#include <algorithm>

namespace engine {
namespace {
BattlePokemon &selfOf(AbilityContext &ctx) {
  return ctx.state
      .teams[static_cast<size_t>(ctx.self.side)][static_cast<size_t>(ctx.self.teamIndex)];
}

class VoltAbsorb final : public Ability {
public:
  const char *name() const override { return "VoltAbsorb"; }
  bool immuneToMove(const Move &move) const override { return move.type == Type::Electric; }
  void onMoveAbsorbed(AbilityContext &ctx) const override {
    BattlePokemon &p = selfOf(ctx);
    int healed = std::min(std::max(1, p.stats.hp / 4), p.stats.hp - p.currentHp);
    if (healed <= 0)
      return;
    p.currentHp += healed;
    ctx.events.emplace_back(HealedEvent{ctx.self, healed});
  }
};

class LightningRod final : public Ability {
public:
  const char *name() const override { return "LightningRod"; }
  bool immuneToMove(const Move &move) const override { return move.type == Type::Electric; }
  void onMoveAbsorbed(AbilityContext &ctx) const override {
    BattlePokemon &p = selfOf(ctx);
    applyStatStageDelta(p, ctx.self, StatIndex::SpA, 1, ctx.events);
  }
};

class FlashFire final : public Ability {
public:
  const char *name() const override { return "FlashFire"; }
  bool immuneToMove(const Move &move) const override { return move.type == Type::Fire; }
  void onMoveAbsorbed(AbilityContext &ctx) const override { selfOf(ctx).flash_fire_active = 1; }
  float damageMultiplier(const Move &move, const BattlePokemon &user) const override {
    return (move.type == Type::Fire && user.flash_fire_active != 0) ? 1.5f : 1.0f;
  }
};

class Bulletproof final : public Ability {
public:
  const char *name() const override { return "Bulletproof"; }
  bool immuneToMove(const Move &move) const override { return move.bulletproof; }
};

class ClearBody final : public Ability {
public:
  const char *name() const override { return "ClearBody"; }
  bool blocksStatDrop() const override { return true; }
};
} // namespace

void registerImmunityAbilities(AbilityTable &table) {
  static const VoltAbsorb voltAbsorb;
  static const LightningRod lightningRod;
  static const FlashFire flashFire;
  static const Bulletproof bulletproof;
  static const ClearBody clearBody;
  auto add = [&table](const Ability &a) { table.push_back(&a); };
  add(voltAbsorb);
  add(lightningRod);
  add(flashFire);
  add(bulletproof);
  add(clearBody);
}
} // namespace engine
