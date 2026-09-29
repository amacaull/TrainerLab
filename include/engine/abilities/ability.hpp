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

// Abilities are stateless singletons: any per-battle state lives in the BattleState POD.
class Ability {
public:
  virtual ~Ability() = default;
  virtual const char *name() const = 0;

  virtual void onSwitchIn(AbilityContext & /*ctx*/) const {}

  virtual void onSwitchOut(AbilityContext & /*ctx*/) const {}

  virtual float damageMultiplier(const Move & /*move*/, const BattlePokemon & /*user*/) const {
    return 1.0f;
  }

  virtual bool immuneToMove(const Move & /*move*/) const { return false; }

  virtual void onMoveAbsorbed(AbilityContext & /*ctx*/) const {}

  virtual float incomingDamageMultiplier(const Move & /*move*/) const { return 1.0f; }

  virtual bool ignoresBurnPenalty() const { return false; }

  virtual void onDamagingHit(EffectContext & /*ctx*/, CombatantRef /*self*/,
                             CombatantRef /*attacker*/, bool /*contact*/) const {}

  virtual float statMultiplier(StatIndex /*stat*/) const { return 1.0f; }

  virtual float speedMultiplier(const BattleState & /*state*/) const { return 1.0f; }

  virtual float stabMultiplier() const { return 1.5f; }

  virtual int priorityBoost(const Move & /*move*/) const { return 0; }

  virtual bool bouncesStatusMoves() const { return false; }

  virtual bool blocksStatDrop() const { return false; }

  virtual void onStatLoweredByOpponent(AbilityContext & /*ctx*/) const {}

  virtual void onAfterKO(AbilityContext & /*ctx*/) const {}

  virtual void onAfterDamagingMove(AbilityContext & /*ctx*/, CombatantRef /*target*/) const {}

  virtual void onHalfHpCrossed(AbilityContext & /*ctx*/, bool /*fromDirectHit*/) const {}

  virtual float opposingSpAMultiplier() const { return 1.0f; }

  virtual bool pressuresPP() const { return false; }

  virtual bool blocksOpposingBerries() const { return false; }

  virtual bool blocksRecoil() const { return false; }

  virtual bool blocksStatus(const BattleState & /*state*/) const { return false; }

  virtual bool ignoresStages() const { return false; }

  virtual bool hasDisguise() const { return false; }
};

const Ability *abilityByName(std::string_view name);

// The ability id travels in the event stream: the registration order is frozen.
const Ability *abilityByIndex(int id);
int findAbilityIdByName(std::string_view name);
int abilityCount();

const Ability *abilityOf(const DataLoader &data, const BattlePokemon &p);

// Fires onHalfHpCrossed exactly once per crossing. Call it at every damage site.
void abilityHpCheck(BattleState &state, const DataLoader &data, const CombatantRef &who,
                    int hpBefore, bool fromDirectHit, EventLog &events);
} // namespace engine
