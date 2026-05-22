#pragma once

#include "engine/effects/effect.hpp"

namespace engine {

// Reads power/type/category from the move in the context.
class DamageEffect : public Effect {
public:
    void apply(EffectContext& ctx) const override;
    const char* name() const override { return "Damage"; }
};

} // namespace engine
