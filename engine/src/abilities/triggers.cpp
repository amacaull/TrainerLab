#include "engine/abilities/registration.hpp"

#include "engine/core/battle_state.hpp"
#include "engine/effects/stat_change.hpp"
#include "engine/items/item.hpp"

namespace engine {
namespace {
BattlePokemon &selfOf(AbilityContext &ctx) {
  return ctx.state
      .teams[static_cast<size_t>(ctx.self.side)][static_cast<size_t>(ctx.self.teamIndex)];
}

class Moxie final : public Ability {
public:
  const char *name() const override { return "Moxie"; }
  void onAfterKO(AbilityContext &ctx) const override {
    ctx.events.emplace_back(AbilityTriggeredEvent{ctx.self, name()});
    applyStatStageDelta(selfOf(ctx), ctx.self, StatIndex::Atk, 1, ctx.events);
  }
};

class Defiant final : public Ability {
public:
  const char *name() const override { return "Defiant"; }
  void onStatLoweredByOpponent(AbilityContext &ctx) const override {
    ctx.events.emplace_back(AbilityTriggeredEvent{ctx.self, name()});
    applyStatStageDelta(selfOf(ctx), ctx.self, StatIndex::Atk, 2, ctx.events);
  }
};

class Berserk final : public Ability {
public:
  const char *name() const override { return "Berserk"; }
  void onHalfHpCrossed(AbilityContext &ctx, bool fromDirectHit) const override {
    if (!fromDirectHit)
      return; // canon: only move damage angers it
    ctx.events.emplace_back(AbilityTriggeredEvent{ctx.self, name()});
    applyStatStageDelta(selfOf(ctx), ctx.self, StatIndex::SpA, 1, ctx.events);
  }
};

class Disguise final : public Ability {
public:
  const char *name() const override { return "Disguise"; }
  bool hasDisguise() const override { return true; }
};

class Pressure final : public Ability {
public:
  const char *name() const override { return "Pressure"; }
  bool pressuresPP() const override { return true; }
};

class Unnerve final : public Ability {
public:
  const char *name() const override { return "Unnerve"; }
  bool blocksOpposingBerries() const override { return true; }
};

class Magician final : public Ability {
public:
  const char *name() const override { return "Magician"; }
  void onAfterDamagingMove(AbilityContext &ctx, CombatantRef targetRef) const override {
    BattlePokemon &self = selfOf(ctx);
    BattlePokemon &target =
        ctx.state
            .teams[static_cast<size_t>(targetRef.side)][static_cast<size_t>(targetRef.teamIndex)];
    if (self.item_id != kNoItem || target.item_id == kNoItem || target.item_consumed != 0)
      return;
    ctx.events.emplace_back(AbilityTriggeredEvent{ctx.self, name()});
    ctx.events.emplace_back(ItemKnockedOffEvent{targetRef, itemByIndex(target.item_id)->name()});
    self.item_id = target.item_id;
    self.item_consumed = 0;
    target.item_id = kNoItem;
  }
};

class Prankster final : public Ability {
public:
  const char *name() const override { return "Prankster"; }
  int priorityBoost(const Move &move) const override {
    return move.category == MoveCategory::Status ? 1 : 0;
  }
};

class MagicBounce final : public Ability {
public:
  const char *name() const override { return "MagicBounce"; }
  bool bouncesStatusMoves() const override { return true; }
};

class RockHead final : public Ability {
public:
  const char *name() const override { return "RockHead"; }
  bool blocksRecoil() const override { return true; }
};
} // namespace

void registerTriggerAbilities(AbilityTable &table) {
  static const Moxie moxie;
  static const Defiant defiant;
  static const Berserk berserk;
  static const Disguise disguise;
  static const Pressure pressure;
  static const Unnerve unnerve;
  static const Magician magician;
  static const Prankster prankster;
  static const MagicBounce magicBounce;
  static const RockHead rockHead;
  auto add = [&table](const Ability &a) { table.push_back(&a); };
  add(moxie);
  add(defiant);
  add(berserk);
  add(disguise);
  add(pressure);
  add(unnerve);
  add(magician);
  add(prankster);
  add(magicBounce);
  add(rockHead);
}
} // namespace engine
