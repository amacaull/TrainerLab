#pragma once

#include "engine/events.hpp"

#include <memory>
#include <nlohmann/json_fwd.hpp>

namespace engine {

struct BattleState;
struct Move;
class RNG;

struct EffectContext {
    BattleState& state;
    RNG& rng;
    EventLog& events;
    CombatantRef user;
    CombatantRef target;
    const Move& move;
};

// Base class for all atomic effects.
// A Move is a list of Effects; the engine applies them in order.
// To add a new effect: subclass Effect, register it in makeEffectFromJson.
class Effect {
public:
    virtual ~Effect() = default;
    virtual void apply(EffectContext& ctx) const = 0;
    virtual const char* name() const = 0;
};

using EffectPtr = std::unique_ptr<Effect>;

// Factory: builds an Effect from a JSON node like {"kind": "Damage", ...}.
EffectPtr makeEffectFromJson(const nlohmann::json& j);

} // namespace engine
