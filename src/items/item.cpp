#include "engine/items/item.hpp"

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

class OrbeVie final : public Item {
public:
  const char *name() const override { return "OrbeVie"; }
  float damageMultiplier() const override { return 1.3f; }
  void onAfterDamagingMove(ItemContext &ctx) const override {
    BattlePokemon &p = holderOf(ctx);
    if (p.isFainted())
      return;
    dealItemDamage(ctx, p, name(), std::max(1, p.stats.hp / 10));
  }
};

class Restes final : public Item {
public:
  const char *name() const override { return "Restes"; }
  void onResidual(ItemContext &ctx) const override {
    BattlePokemon &p = holderOf(ctx);
    healFromItem(ctx, p, name(), std::max(1, p.stats.hp / 16));
  }
};

class Detritus final : public Item {
public:
  const char *name() const override { return "Detritus"; }
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

class OrbeFlamme final : public Item {
public:
  const char *name() const override { return "OrbeFlamme"; }
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

class BaieSitrus final : public Item {
public:
  const char *name() const override { return "BaieSitrus"; }
  void onHpChanged(ItemContext &ctx) const override {
    BattlePokemon &p = holderOf(ctx);
    if (p.isFainted() || p.currentHp > p.stats.hp / 2)
      return;
    p.item_consumed = 1; // eaten: heldItem() now returns nullptr
    ctx.events.emplace_back(ItemConsumedEvent{ctx.holder, name()});
    int healed = std::min(std::max(1, p.stats.hp / 4), p.stats.hp - p.currentHp);
    p.currentHp += healed;
    ctx.events.emplace_back(HealedEvent{ctx.holder, healed});
  }
};

class CeintureForce final : public Item {
public:
  const char *name() const override { return "CeintureForce"; }
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

class Massue final : public Item {
public:
  const char *name() const override { return "Massue"; }
  float statMultiplier(StatIndex stat) const override {
    return stat == StatIndex::Atk ? 2.0f : 1.0f;
  }
};

class GrossesBottes final : public Item {
public:
  const char *name() const override { return "GrossesBottes"; }
  bool ignoresHazards() const override { return true; }
};

// Registered now, wired later: DesPipes by the multi-hit engine (phase 14),
// Lumargile by Voile Aurore (phase 12).
class InertItem final : public Item {
public:
  explicit InertItem(const char *itemName) : name_(itemName) {}
  const char *name() const override { return name_; }

private:
  const char *name_;
};

// FROZEN ORDER (ADR #45): item_id crosses the FFI. Append only.
const std::array<const Item *, 13> kItems = {
    new OrbeVie,                                     // 0
    new Restes,                                      // 1
    new Detritus,                                    // 2
    new OrbeFlamme,                                  // 3
    new BaieSitrus,                                  // 4
    new CeintureForce,                               // 5
    new ChoiceItem("BandeauChoix", StatIndex::Atk),  // 6
    new ChoiceItem("LunettesChoix", StatIndex::SpA), // 7
    new ChoiceItem("MouchoirChoix", StatIndex::Spe), // 8
    new GrossesBottes,                               // 9
    new InertItem("DesPipes"),                       // 10
    new Massue,                                      // 11
    new InertItem("Lumargile"),                      // 12
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
  if (const Item *item = heldItem(p)) {
    ItemContext ctx{state, data, events, holder};
    item->onHpChanged(ctx);
  }
}

} // namespace engine
