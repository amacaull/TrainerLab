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

// FlashFire: immune to Fire; the first absorbed Fire move lights the boost,
// which lives in the POD (flash_fire_active) and dies on switch-out.
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
  static const VoltAbsorb absorbeVolt;
  static const LightningRod paratonnerre;
  static const FlashFire torche;
  static const Bulletproof pareBalles;
  static const ClearBody corpsSain;
  auto add = [&table](const Ability &a) { table.push_back(&a); };
  add(absorbeVolt);
  add(paratonnerre);
  add(torche);
  add(pareBalles);
  add(corpsSain);
}

} // namespace engine
