#pragma once

#include "engine/core/events.hpp"
#include "engine/effects/effect.hpp"
#include "engine/model/move.hpp"
#include "engine/model/pokemon.hpp"

#include <string_view>

namespace engine {

struct BattleState;
class DataLoader;

struct AbilityContext {
  BattleState &state;
  const DataLoader &data;
  EventLog &events;
  CombatantRef self;
};

// Abilities are stateless listeners on engine hooks (ADR #9). Any per-battle
// ability state must live in the BattleState POD, never in the Ability itself.
// To add an ability: subclass, then register it in abilityByName (ability.cpp).
class Ability {
public:
  virtual ~Ability() = default;
  virtual const char *name() const = 0;

  // Fired on any switch-in: Switch action, pivot, KO replacement, battle start.
  virtual void onSwitchIn(AbilityContext & /*ctx*/) const {}

  // Multiplier on the holder's outgoing damage (Blaze/Torrent/Overgrow).
  virtual float damageMultiplier(const Move & /*move*/, const BattlePokemon & /*user*/) const {
    return 1.0f;
  }

  // Full immunity to an incoming move (Levitate vs Ground).
  virtual bool immuneToMove(const Move & /*move*/) const { return false; }

  // Multiplier on damage RECEIVED by the holder (Thick Fat: Fire/Ice x0.5).
  virtual float incomingDamageMultiplier(const Move & /*move*/) const { return 1.0f; }

  // Guts: the burn physical-damage halving does not apply to the holder.
  virtual bool ignoresBurnPenalty() const { return false; }

  // Fired when the holder is damaged by a move; contact tells whether the
  // move makes contact (Static 30% paralysis, Rough Skin 1/8 chip).
  virtual void onDamagingHit(EffectContext & /*ctx*/, CombatantRef /*self*/,
                             CombatantRef /*attacker*/, bool /*contact*/) const {}
};

// Registry lookup. Returns nullptr for unknown or empty names. Abilities are
// const singletons; the same instance serves every battle.
const Ability *abilityByName(std::string_view name);

} // namespace engine
