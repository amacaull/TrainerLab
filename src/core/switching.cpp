#include "engine/core/switching.hpp"

#include "engine/abilities/ability.hpp"
#include "engine/core/battle_state.hpp"
#include "engine/core/data_loader.hpp"
#include "engine/items/item.hpp"
#include "engine/model/status.hpp"
#include "engine/model/types.hpp"

#include <algorithm>
#include <string_view>

namespace engine {

bool isValidSwitchTarget(const BattleState &state, int side, int teamIndex) {
  if (teamIndex < 0 || teamIndex >= state.team_size[static_cast<size_t>(side)])
    return false;
  if (teamIndex == state.activeIndex[static_cast<size_t>(side)])
    return false;
  const BattlePokemon &p = state.teams[static_cast<size_t>(side)][static_cast<size_t>(teamIndex)];
  return !p.isEmpty() && !p.isFainted();
}

int firstHealthyBenched(const BattleState &state, int side) {
  for (int i = 0; i < state.team_size[static_cast<size_t>(side)]; ++i) {
    if (isValidSwitchTarget(state, side, i))
      return i;
  }
  return -1;
}

namespace {

bool hasType(const Species &sp, Type t) { return sp.type1 == t || sp.type2 == t; }

void dealHazardDamage(BattlePokemon &in, CombatantRef ref, HazardKind kind, int damage,
                      EventLog &events) {
  in.currentHp = std::max(0, in.currentHp - damage);
  events.emplace_back(HazardDamageEvent{ref, kind, damage});
  if (in.isFainted())
    events.emplace_back(FaintedEvent{ref});
}

// Canon application order on entry: Stealth Rock, Spikes, Toxic Spikes.
void applyEntryHazards(BattleState &state, const DataLoader &data, int side, EventLog &events) {
  BattlePokemon &in = state.active(side);
  const Item *item = heldItem(in);
  const bool boots = item != nullptr && item->ignoresHazards();
  const Species &sp = data.speciesByIndex(in.species_id);
  SideHazards &hz = state.hazards[static_cast<size_t>(side)];
  CombatantRef ref{side, state.activeIndex[static_cast<size_t>(side)]};

  // Heavy-Duty Boots skip every hazard, but a grounded Poison-type still
  // soaks up the Toxic Spikes (canon).
  if (boots) {
    if (hz.toxic_spikes > 0 && isGrounded(sp) && hasType(sp, Type::Poison)) {
      hz.toxic_spikes = 0;
      events.emplace_back(ToxicSpikesAbsorbedEvent{ref});
    }
    return;
  }

  if (hz.stealth_rock > 0) {
    float eff = data.typeChart().effectiveness(Type::Rock, sp.type1, sp.type2);
    int damage = std::max(1, static_cast<int>(static_cast<float>(in.stats.hp) / 8.0f * eff));
    dealHazardDamage(in, ref, HazardKind::StealthRock, damage, events);
    if (in.isFainted())
      return;
  }

  if (!isGrounded(sp))
    return;

  if (hz.spikes > 0) {
    int denom = (hz.spikes == 1) ? 8 : (hz.spikes == 2) ? 6 : 4;
    int damage = std::max(1, in.stats.hp / denom);
    dealHazardDamage(in, ref, HazardKind::Spikes, damage, events);
    if (in.isFainted())
      return;
  }

  if (hz.toxic_spikes > 0) {
    if (hasType(sp, Type::Poison)) {
      // A grounded Poison-type soaks up the Toxic Spikes (canon).
      hz.toxic_spikes = 0;
      events.emplace_back(ToxicSpikesAbsorbedEvent{ref});
    } else if (in.status == Status::None && !hasType(sp, Type::Steel)) {
      in.status = (hz.toxic_spikes >= 2) ? Status::Toxic : Status::Poison;
      in.status_turns = 0;
      events.emplace_back(StatusAppliedEvent{ref, in.status});
    }
  }
}

} // namespace

void performSwitch(BattleState &state, const DataLoader &data, int side, int newIndex,
                   EventLog &events) {
  BattlePokemon &out = state.active(side);

  CombatantRef exitingRef{side, state.activeIndex[static_cast<size_t>(side)]};
  if (const Ability *outAbility = abilityOf(data, out)) {
    // Regenerator / NaturalCure fire only if the holder walks out standing.
    if (!out.isFainted()) {
      AbilityContext actx{state, data, events, exitingRef};
      outAbility->onSwitchOut(actx);
    }
    // Delta Stream is presence-bound: it clears when its holder leaves,
    // faint included (ADR #47).
    if (std::string_view(outAbility->name()) == "DeltaStream" &&
        state.weather == Weather::StrongWinds) {
      events.emplace_back(WeatherEndedEvent{state.weather});
      state.weather = Weather::None;
      state.weather_turns_left = 0;
    }
  }
  // Canon: stat stages and the Toxic ramp counter reset on switch-out;
  // sleep turns and every status itself persist.
  out.stat_stages = {};
  if (out.status == Status::Toxic)
    out.status_turns = 0;
  out.flinched = 0;
  out.roosted = 0;
  out.protected_now = 0;
  out.protect_contact_status = 0;
  out.destiny_bond_active = 0;
  out.last_move_id = kNoMove;
  out.protect_chain = 0;
  out.charging_move_id = kNoMove;
  out.invulnerable_state = 0;
  out.locked_move_id = kNoMove; // the Choice lock ends when the holder leaves
  out.flash_fire_active = 0;    // FlashFire's boost dies with the exit

  CombatantRef outRef{side, state.activeIndex[static_cast<size_t>(side)]};
  events.emplace_back(SwitchedOutEvent{outRef});

  state.activeIndex[static_cast<size_t>(side)] = newIndex;
  CombatantRef inRef{side, newIndex};
  events.emplace_back(SwitchedInEvent{inRef});

  int hpBeforeHazards = state.active(side).currentHp;
  state.active(side).turns_on_field = 0; // opens the FakeOut window
  applyEntryHazards(state, data, side, events);

  // A Pokemon that faints to hazards never gets its ability off (canon).
  if (state.active(side).isFainted())
    return;

  itemHpCheck(state, data, inRef, events);
  abilityHpCheck(state, data, inRef, hpBeforeHazards, false, events);

  // EmergencyExit may have swapped again from hazard damage: the nested
  // performSwitch already ran the full entry sequence for the newcomer.
  if (state.activeIndex[static_cast<size_t>(side)] != newIndex)
    return;

  const Species &sp = data.speciesByIndex(state.active(side).species_id);
  if (const Ability *ability = abilityByName(sp.ability)) {
    AbilityContext ctx{state, data, events, inRef};
    ability->onSwitchIn(ctx);
  }
}

bool isGrounded(const Species &sp) {
  if (sp.type1 == Type::Flying || sp.type2 == Type::Flying)
    return false;
  const Ability *ab = abilityByName(sp.ability);
  return !(ab && std::string_view(ab->name()) == "Levitate");
}

} // namespace engine
