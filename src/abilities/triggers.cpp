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
  static const Moxie impudence;
  static const Defiant acharne;
  static const Berserk colerique;
  static const Disguise fantomasque;
  static const Pressure pression;
  static const Unnerve tension;
  static const Magician magicien;
  static const Prankster farceur;
  static const MagicBounce miroirMagik;
  static const RockHead teteDeRoc;
  auto add = [&table](const Ability &a) { table.push_back(&a); };
  add(impudence);
  add(acharne);
  add(colerique);
  add(fantomasque);
  add(pression);
  add(tension);
  add(magicien);
  add(farceur);
  add(miroirMagik);
  add(teteDeRoc);
}

} // namespace engine
