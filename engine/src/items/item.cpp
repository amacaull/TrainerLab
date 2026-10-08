#include "engine/items/item.hpp"

#include "engine/abilities/ability.hpp"
#include "engine/core/battle_state.hpp"
#include "engine/core/data_loader.hpp"
#include "engine/model/status.hpp"

#include <algorithm>
#include <array>
#include <string>

namespace engine {
namespace {
BattlePokemon &holderOf(ItemContext &ctx) {
  return ctx.state
      .teams[static_cast<size_t>(ctx.holder.side)][static_cast<size_t>(ctx.holder.teamIndex)];
}

void dealItemDamage(ItemContext &ctx, BattlePokemon &p, const char *itemName, int damage) {
  p.currentHp = std::max(0, p.currentHp - damage);
  ctx.events.emplace_back(ItemDamageEvent{ctx.holder, itemName, damage});
  if (p.isFainted())
    ctx.events.emplace_back(FaintedEvent{ctx.holder});
}

void healFromItem(ItemContext &ctx, BattlePokemon &p, const char *itemName, int amount) {
  int healed = std::min(amount, p.stats.hp - p.currentHp);
  if (healed <= 0)
    return;
  p.currentHp += healed;
  ctx.events.emplace_back(ItemTriggeredEvent{ctx.holder, itemName});
  ctx.events.emplace_back(HealedEvent{ctx.holder, healed});
}

class LifeOrb final : public Item {
public:
  const char *name() const override { return "LifeOrb"; }
  float damageMultiplier() const override { return 1.3f; }
  void onAfterDamagingMove(ItemContext &ctx) const override {
    BattlePokemon &p = holderOf(ctx);
    if (p.isFainted())
      return;
    dealItemDamage(ctx, p, name(), std::max(1, p.stats.hp / 10));
  }
};

class Leftovers final : public Item {
public:
  const char *name() const override { return "Leftovers"; }
  void onResidual(ItemContext &ctx) const override {
    BattlePokemon &p = holderOf(ctx);
    healFromItem(ctx, p, name(), std::max(1, p.stats.hp / 16));
  }
};

class BlackSludge final : public Item {
public:
  const char *name() const override { return "BlackSludge"; }
  void onResidual(ItemContext &ctx) const override {
    BattlePokemon &p = holderOf(ctx);
    const Species &sp = ctx.data.speciesByIndex(p.species_id);
    if (sp.type1 == Type::Poison || sp.type2 == Type::Poison) {
      healFromItem(ctx, p, name(), std::max(1, p.stats.hp / 16));
    } else {
      dealItemDamage(ctx, p, name(), std::max(1, p.stats.hp / 8));
    }
  }
};

class FlameOrb final : public Item {
public:
  const char *name() const override { return "FlameOrb"; }
  void onTurnEnd(ItemContext &ctx) const override {
    BattlePokemon &p = holderOf(ctx);
    if (p.isFainted() || p.status != Status::None)
      return;
    const Species &sp = ctx.data.speciesByIndex(p.species_id);
    if (typeImmuneToStatus(Status::Burn, sp))
      return;
    p.status = Status::Burn;
    ctx.events.emplace_back(ItemTriggeredEvent{ctx.holder, name()});
    ctx.events.emplace_back(StatusAppliedEvent{ctx.holder, Status::Burn});
  }
};

class SitrusBerry final : public Item {
public:
  const char *name() const override { return "SitrusBerry"; }
  void onHpChanged(ItemContext &ctx) const override {
    BattlePokemon &p = holderOf(ctx);
    if (p.isFainted() || p.currentHp > p.stats.hp / 2)
      return;
    p.item_consumed = 1;
    ctx.events.emplace_back(ItemConsumedEvent{ctx.holder, name()});
    int healed = std::min(std::max(1, p.stats.hp / 4), p.stats.hp - p.currentHp);
    p.currentHp += healed;
    ctx.events.emplace_back(HealedEvent{ctx.holder, healed});
  }
};

class FocusSash final : public Item {
public:
  const char *name() const override { return "FocusSash"; }
  int adjustLethalDamage(ItemContext &ctx, int damage) const override {
    BattlePokemon &p = holderOf(ctx);
    if (p.currentHp != p.stats.hp || damage < p.currentHp)
      return damage;
    p.item_consumed = 1;
    ctx.events.emplace_back(ItemConsumedEvent{ctx.holder, name()});
    return p.currentHp - 1;
  }
};

class ChoiceItem : public Item {
public:
  ChoiceItem(const char *itemName, StatIndex boosted) : name_(itemName), boosted_(boosted) {}
  const char *name() const override { return name_; }
  float statMultiplier(StatIndex stat) const override { return stat == boosted_ ? 1.5f : 1.0f; }
  bool locksMove() const override { return true; }

private:
  const char *name_;
  StatIndex boosted_;
};

class ThickClub final : public Item {
public:
  const char *name() const override { return "ThickClub"; }
  float statMultiplier(StatIndex stat) const override {
    return stat == StatIndex::Atk ? 2.0f : 1.0f;
  }
};

class HeavyDutyBoots final : public Item {
public:
  const char *name() const override { return "HeavyDutyBoots"; }
  bool ignoresHazards() const override { return true; }
};

class LightClay final : public Item {
public:
  const char *name() const override { return "LightClay"; }
  int screenDuration(int base) const override { return base + 3; }
};

// No hook of its own: MultiHitEffect reads it by name (LoadedDice).
class InertItem final : public Item {
public:
  explicit InertItem(const char *itemName) : name_(itemName) {}
  const char *name() const override { return name_; }

private:
  const char *name_;
};

// Frozen order: item_id crosses the FFI. Append only.
const LifeOrb kLifeOrb;
const Leftovers kLeftovers;
const BlackSludge kBlackSludge;
const FlameOrb kFlameOrb;
const SitrusBerry kSitrusBerry;
const FocusSash kFocusSash;
const ChoiceItem kChoiceBand{"ChoiceBand", StatIndex::Atk};
const ChoiceItem kChoiceSpecs{"ChoiceSpecs", StatIndex::SpA};
const ChoiceItem kChoiceScarf{"ChoiceScarf", StatIndex::Spe};
const HeavyDutyBoots kHeavyDutyBoots;
const InertItem kLoadedDice{"LoadedDice"};
const ThickClub kThickClub;
const LightClay kLightClay;

const std::array<const Item *, 13> kItems = {
    &kLifeOrb,        // 0
    &kLeftovers,      // 1
    &kBlackSludge,    // 2
    &kFlameOrb,       // 3
    &kSitrusBerry,    // 4
    &kFocusSash,      // 5
    &kChoiceBand,     // 6
    &kChoiceSpecs,    // 7
    &kChoiceScarf,    // 8
    &kHeavyDutyBoots, // 9
    &kLoadedDice,     // 10
    &kThickClub,      // 11
    &kLightClay,      // 12
};
} // namespace

const Item *itemByIndex(int id) {
  if (id < 0 || id >= itemCount())
    return nullptr;
  return kItems[static_cast<size_t>(id)];
}

int findItemIdByName(std::string_view name) {
  for (size_t i = 0; i < kItems.size(); ++i)
    if (name == kItems[i]->name())
      return static_cast<int>(i);
  return -1;
}

int itemCount() { return static_cast<int>(kItems.size()); }

const Item *heldItem(const BattlePokemon &p) {
  if (p.item_id == kNoItem || p.item_consumed != 0)
    return nullptr;
  return itemByIndex(p.item_id);
}

void itemHpCheck(BattleState &state, const DataLoader &data, const CombatantRef &holder,
                 EventLog &events) {
  const BattlePokemon &p =
      state.teams[static_cast<size_t>(holder.side)][static_cast<size_t>(holder.teamIndex)];
  const BattlePokemon &foe = state.active(1 - holder.side);
  if (!foe.isFainted()) {
    if (const Ability *foeAbility = abilityOf(data, foe)) {
      if (foeAbility->blocksOpposingBerries())
        return;
    }
  }
  if (const Item *item = heldItem(p)) {
    ItemContext ctx{state, data, events, holder};
    item->onHpChanged(ctx);
  }
}
} // namespace engine
