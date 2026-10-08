#pragma once

#include "engine/core/events.hpp"
#include "engine/model/pokemon.hpp"

#include <string_view>

namespace engine {
struct BattleState;
class DataLoader;

struct ItemContext {
  BattleState &state;
  const DataLoader &data;
  EventLog &events;
  CombatantRef holder;
};

// Items are stateless singletons: any per-battle state lives in the BattleState POD.
class Item {
public:
  virtual ~Item() = default;
  virtual const char *name() const = 0;

  virtual float statMultiplier(StatIndex) const { return 1.0f; }

  virtual float damageMultiplier() const { return 1.0f; }

  virtual void onAfterDamagingMove(ItemContext &) const {}

  // Before the status residuals: Leftovers heals before the poison ticks (canon).
  virtual void onResidual(ItemContext &) const {}

  // After every residual: the orb's burn only deals damage from the next turn (canon).
  virtual void onTurnEnd(ItemContext &) const {}

  virtual void onHpChanged(ItemContext &) const {}

  virtual int adjustLethalDamage(ItemContext &, int damage) const { return damage; }

  virtual bool ignoresHazards() const { return false; }

  virtual bool locksMove() const { return false; }

  virtual int screenDuration(int base) const { return base; }
};

// The registration order in item.cpp is frozen: item_id crosses the FFI.
const Item *itemByIndex(int id);
int findItemIdByName(std::string_view name);
int itemCount();

const Item *heldItem(const BattlePokemon &p);

// Call after every damage site, so berries trigger on any HP drop.
void itemHpCheck(BattleState &state, const DataLoader &data, const CombatantRef &holder,
                 EventLog &events);
} // namespace engine
