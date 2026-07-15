#include "engine/struggle.hpp"

#include "engine/battle_state.hpp"
#include "engine/effect.hpp"
#include "engine/effects/damage.hpp"
#include "engine/events.hpp"
#include "engine/pokemon.hpp"

#include <algorithm>
#include <memory>

namespace engine {

namespace {

// Unlike RecoilEffect (fraction of damage dealt), Struggle's recoil is a
// flat quarter of the user's max HP, only if the hit connected.
class StruggleRecoilEffect : public Effect {
public:
  void apply(EffectContext &ctx) const override {
    if (ctx.lastDamageDealt <= 0)
      return;
    BattlePokemon &user =
        ctx.state
            .teams[static_cast<size_t>(ctx.user.side)][static_cast<size_t>(ctx.user.teamIndex)];
    int damage = std::max(1, user.stats.hp / 4);
    user.currentHp = std::max(0, user.currentHp - damage);
    ctx.events.emplace_back(RecoilDamageEvent{ctx.user, damage});
    if (user.isFainted())
      ctx.events.emplace_back(FaintedEvent{ctx.user});
  }
  const char *name() const override { return "StruggleRecoil"; }
};

Move buildStruggle() {
  Move m;
  m.name = "Lutte";
  m.category = MoveCategory::Physical;
  m.power = 50;
  m.accuracy = 0; // never-miss sentinel
  m.typeless = true;
  m.makesContact = true;
  m.effects.push_back(std::make_unique<DamageEffect>());
  m.effects.push_back(std::make_unique<StruggleRecoilEffect>());
  return m;
}

} // namespace

const Move &struggleMove() {
  static const Move kStruggle = buildStruggle();
  return kStruggle;
}

} // namespace engine
