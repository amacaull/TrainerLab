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
// To add an ability: subclass in the matching family file (damage_mods,
// immunities, weather_abilities, switch_hooks, triggers), then add it to that
// family's register function. New abilities use canon French ASCII names
// (ADR #40); the 12 legacy English ones die with the test roster in phase 14.
class Ability {
public:
  virtual ~Ability() = default;
  virtual const char *name() const = 0;

  // Fired on any switch-in: Switch action, pivot, KO replacement, battle start.
  virtual void onSwitchIn(AbilityContext & /*ctx*/) const {}

  // Fired on a voluntary or pivot switch-out, holder still standing
  // (Regenerator +1/3 HP, NaturalCure status purge). Not fired on faint.
  virtual void onSwitchOut(AbilityContext & /*ctx*/) const {}

  // Multiplier on the holder's outgoing damage (Blaze pinch, Technician,
  // ToughClaws, IronFist, Sharpness, FlashFire when lit).
  virtual float damageMultiplier(const Move & /*move*/, const BattlePokemon & /*user*/) const {
    return 1.0f;
  }

  // Full immunity to an incoming move (Levitate vs Ground, VoltAbsorb,
  // FlashFire, Bulletproof). onMoveAbsorbed then fires for the side effect.
  virtual bool immuneToMove(const Move & /*move*/) const { return false; }

  // Side effect of an absorbed move (VoltAbsorb heals 25%, LightningRod
  // +1 SpA, FlashFire lights flash_fire_active).
  virtual void onMoveAbsorbed(AbilityContext & /*ctx*/) const {}

  // Multiplier on damage RECEIVED by the holder (Thick Fat: Fire/Ice x0.5).
  virtual float incomingDamageMultiplier(const Move & /*move*/) const { return 1.0f; }

  // Guts: the burn physical-damage halving does not apply to the holder.
  virtual bool ignoresBurnPenalty() const { return false; }

  // Fired when the holder is damaged by a move; contact tells whether the
  // move makes contact (Static 30% paralysis, Rough Skin 1/8 chip).
  virtual void onDamagingHit(EffectContext & /*ctx*/, CombatantRef /*self*/,
                             CombatantRef /*attacker*/, bool /*contact*/) const {}

  // Flat stat multiplier, after stages (HugePower: Atk x2).
  virtual float statMultiplier(StatIndex /*stat*/) const { return 1.0f; }

  // Speed multiplier that may depend on the field (SwiftSwim, SandRush,
  // SlushRush: x2 under their weather).
  virtual float speedMultiplier(const BattleState & /*state*/) const { return 1.0f; }

  // STAB value for the holder's attacks (Adaptability: 2.0).
  virtual float stabMultiplier() const { return 1.5f; }

  // Priority boost per move (Prankster: +1 on Status moves).
  virtual int priorityBoost(const Move & /*move*/) const { return 0; }

  // MagicBounce: `reflectable` moves aimed at the holder re-run with the
  // roles swapped, skipping Protect and accuracy (canon).
  virtual bool bouncesStatusMoves() const { return false; }

  // ClearBody: stat drops inflicted by opponents fail.
  virtual bool blocksStatDrop() const { return false; }

  // Defiant: fired after an opponent lowered one of the holder's stats.
  virtual void onStatLoweredByOpponent(AbilityContext & /*ctx*/) const {}

  // Moxie: fired when the holder's damaging move KOs the target.
  virtual void onAfterKO(AbilityContext & /*ctx*/) const {}

  // Fired right after the holder's damaging move connected (Magician steals
  // the target's item if the holder has none).
  virtual void onAfterDamagingMove(AbilityContext & /*ctx*/, CombatantRef /*target*/) const {}

  // Fired when damage takes the holder from above to at-or-below half HP.
  // fromDirectHit is true only for move damage (Berserk cares; the
  // EmergencyExit auto-switch does not).
  virtual void onHalfHpCrossed(AbilityContext & /*ctx*/, bool /*fromDirectHit*/) const {}

  // VesselOfRuin: the opponent's Special Attack is scaled while the holder
  // is on the field.
  virtual float opposingSpAMultiplier() const { return 1.0f; }

  // Pressure: moves targeting the holder cost one extra PP.
  virtual bool pressuresPP() const { return false; }

  // Unnerve: opposing berries never trigger.
  virtual bool blocksOpposingBerries() const { return false; }

  // RockHead: no recoil from the holder's recoil moves (Struggle excepted).
  virtual bool blocksRecoil() const { return false; }

  // LeafGuard: no status under the sun.
  virtual bool blocksStatus(const BattleState & /*state*/) const { return false; }

  // Unaware: stat stages of the other side are ignored in damage, both ways.
  virtual bool ignoresStages() const { return false; }

  // Disguise: the first direct hit is blocked instead (POD flag
  // disguise_broken, 1/8 max-HP chip, gen 8 rules).
  virtual bool hasDisguise() const { return false; }
};

// Registry lookup. Returns nullptr for unknown or empty names. Abilities are
// const singletons; the same instance serves every battle.
const Ability *abilityByName(std::string_view name);

// Ability of a combatant's species, nullptr if none (Zoroark, ADR #43).
const Ability *abilityOf(const DataLoader &data, const BattlePokemon &p);

// Half-HP crossing check shared by every damage application site: fires
// onHalfHpCrossed (Berserk, EmergencyExit) exactly once per crossing.
void abilityHpCheck(BattleState &state, const DataLoader &data, const CombatantRef &who,
                    int hpBefore, bool fromDirectHit, EventLog &events);

} // namespace engine
