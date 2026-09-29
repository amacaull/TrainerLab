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

// Items follow the ability pattern (ADR #34): stateless const singletons,
// hooks with no-op defaults, all per-battle state in the POD (item_id,
// item_consumed, locked_move_id). Unlike abilities they cross the FFI as
// indices, so the catalog order below is frozen (ADR #45).
class Item {
public:
  virtual ~Item() = default;
  virtual const char *name() const = 0;

  // Stat multiplier applied after stages (Choice items, ThickClub).
  virtual float statMultiplier(StatIndex) const { return 1.0f; }

  // Outgoing damage multiplier on damaging moves (Life Orb).
  virtual float damageMultiplier() const { return 1.0f; }

  // After the holder's damaging move connected (Life Orb recoil).
  virtual void onAfterDamagingMove(ItemContext &) const {}

  // End of turn, before status residuals (Leftovers, BlackSludge) — canon order:
  // Leftovers heals before the poison ticks.
  virtual void onResidual(ItemContext &) const {}

  // End of turn, after all residuals (FlameOrb) — canon: the orb's burn
  // only starts dealing damage on the next turn.
  virtual void onTurnEnd(ItemContext &) const {}

  // Called after any HP drop (move, hazard, chip, residual): SitrusBerry.
  virtual void onHpChanged(ItemContext &) const {}

  // Lethal-hit interception (FocusSash). Returns the adjusted damage.
  virtual int adjustLethalDamage(ItemContext &, int damage) const { return damage; }

  // HeavyDutyBoots: entry hazards don't apply at all.
  virtual bool ignoresHazards() const { return false; }

  // Choice items: the holder is locked into its first move until it leaves.
  virtual bool locksMove() const { return false; }

  // Screen duration set by this holder (LightClay: 5 -> 8 turns).
  virtual int screenDuration(int base) const { return base; }
};

// Indexed catalog (ADR #45): registration order in item.cpp is frozen —
// item_id crosses the FFI. Lookup by name happens at load time only.
const Item *itemByIndex(int id);
int findItemIdByName(std::string_view name); // -1 on miss
int itemCount();

// The item actually in hand: nullptr if none, consumed, or knocked off.
const Item *heldItem(const BattlePokemon &p);

// Fires the holder's HP-threshold hook (SitrusBerry). Call after every
// damage application site so berries trigger on any crossing, not just
// direct hits.
void itemHpCheck(BattleState &state, const DataLoader &data, const CombatantRef &holder,
                 EventLog &events);

} // namespace engine
