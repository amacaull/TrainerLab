#include "engine/abilities/registration.hpp"

#include "engine/core/battle_state.hpp"
#include "engine/core/switching.hpp"

#include <algorithm>

namespace engine {

namespace {

BattlePokemon &selfOf(AbilityContext &ctx) {
  return ctx.state
      .teams[static_cast<size_t>(ctx.self.side)][static_cast<size_t>(ctx.self.teamIndex)];
}

class Regenerator final : public Ability {
public:
  const char *name() const override { return "Regenerator"; }
  void onSwitchOut(AbilityContext &ctx) const override {
    BattlePokemon &p = selfOf(ctx);
    int healed = std::min(std::max(1, p.stats.hp / 3), p.stats.hp - p.currentHp);
    if (healed <= 0)
      return;
    p.currentHp += healed;
    ctx.events.emplace_back(AbilityTriggeredEvent{ctx.self, name()});
    ctx.events.emplace_back(HealedEvent{ctx.self, healed});
  }
};

class NaturalCure final : public Ability {
public:
  const char *name() const override { return "NaturalCure"; }
  void onSwitchOut(AbilityContext &ctx) const override {
    BattlePokemon &p = selfOf(ctx);
    if (p.status == Status::None)
      return;
    p.status = Status::None;
    p.status_turns = 0;
    p.sleep_self_inflicted = 0;
    ctx.events.emplace_back(AbilityTriggeredEvent{ctx.self, name()});
  }
};

// EmergencyExit auto-switches to the first healthy benched teammate when
// crossing below half HP. Canon lets the player pick; a stateless
// resolveTurn cannot ask mid-turn, so the divergence is assumed (ADR #47).
class EmergencyExit final : public Ability {
public:
  const char *name() const override { return "EmergencyExit"; }
  void onHalfHpCrossed(AbilityContext &ctx, bool /*fromDirectHit*/) const override {
    int side = ctx.self.side;
    if (ctx.state.activeIndex[static_cast<size_t>(side)] != ctx.self.teamIndex)
      return; // already benched (pivoted out mid-chain)
    int target = firstHealthyBenched(ctx.state, side);
    if (target < 0)
      return;
    ctx.events.emplace_back(AbilityTriggeredEvent{ctx.self, name()});
    performSwitch(ctx.state, ctx.data, side, target, ctx.events);
  }
};

} // namespace

void registerSwitchHookAbilities(AbilityMap &map) {
  static const Regenerator regeForce;
  static const NaturalCure medicNature;
  static const EmergencyExit repliTactique;
  auto add = [&map](const Ability &a) { map.emplace(a.name(), &a); };
  add(regeForce);
  add(medicNature);
  add(repliTactique);
}

} // namespace engine
