#include "engine/effects/damage.hpp"

#include "engine/battle_state.hpp"
#include "engine/move.hpp"
#include "engine/pokemon.hpp"
#include "engine/rng.hpp"
#include "engine/types.hpp"

#include <algorithm>
#include <cmath>

namespace engine {

// TODO (phase 1): move TypeChart into EffectContext, remove this global.
extern const TypeChart* g_typeChart;

void DamageEffect::apply(EffectContext& ctx) const {
    const Move& move = ctx.move;
    if (move.power <= 0) return;

    BattlePokemon& attacker = ctx.state.teams[static_cast<size_t>(ctx.user.side)][static_cast<size_t>(ctx.user.teamIndex)];
    BattlePokemon& defender = ctx.state.teams[static_cast<size_t>(ctx.target.side)][static_cast<size_t>(ctx.target.teamIndex)];

    int atkStat, defStat;
    if (move.category == MoveCategory::Physical) {
        atkStat = attacker.stats.atk;
        defStat = defender.stats.def;
    } else if (move.category == MoveCategory::Special) {
        atkStat = attacker.stats.specAtk;
        defStat = defender.stats.specDef;
    } else {
        return;
    }

    // Canonical formula (gen 5+):
    //   base = floor( ((2*level/5 + 2) * power * Atk/Def) / 50 ) + 2
    // Then modifiers: STAB, type effectiveness, random 0.85..1.0.
    int level = attacker.level;
    float base = (((2.0f * static_cast<float>(level) / 5.0f) + 2.0f)
                  * static_cast<float>(move.power)
                  * static_cast<float>(atkStat)
                  / static_cast<float>(defStat))
                 / 50.0f + 2.0f;

    bool stab = (move.type == attacker.species->type1 || move.type == attacker.species->type2);
    float stabMul = stab ? 1.5f : 1.0f;

    float typeMul = 1.0f;
    if (g_typeChart != nullptr) {
        typeMul = g_typeChart->effectiveness(move.type, defender.species->type1, defender.species->type2);
    }

    float randMul = static_cast<float>(ctx.rng.rangeInt(85, 100)) / 100.0f;

    float total = base * stabMul * typeMul * randMul;
    int damage = std::max(1, static_cast<int>(std::floor(total)));
    if (typeMul == 0.0f) damage = 0;

    defender.currentHp = std::max(0, defender.currentHp - damage);

    ctx.events.emplace_back(DamageDealtEvent{ctx.target, damage, typeMul, stab});

    if (defender.isFainted()) {
        ctx.events.emplace_back(FaintedEvent{ctx.target});
    }
}

} // namespace engine
