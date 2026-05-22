#include "engine/types.hpp"

#include <array>
#include <stdexcept>
#include <string>

namespace engine {

namespace {
constexpr std::array<std::string_view, TypeCount> kTypeNames = {
    "Normal", "Fire", "Water", "Electric", "Grass", "Ice",
    "Fighting", "Poison", "Ground", "Flying", "Psychic", "Bug",
    "Rock", "Ghost", "Dragon", "Dark", "Steel", "Fairy"
};
} // namespace

std::string_view typeName(Type t) {
    int idx = static_cast<int>(t);
    if (idx < 0 || idx >= TypeCount) {
        throw std::out_of_range("typeName: index out of range");
    }
    return kTypeNames[static_cast<size_t>(idx)];
}

Type typeFromString(std::string_view s) {
    for (int i = 0; i < TypeCount; ++i) {
        if (kTypeNames[static_cast<size_t>(i)] == s) {
            return static_cast<Type>(i);
        }
    }
    throw std::invalid_argument("typeFromString: unknown type '" + std::string(s) + "'");
}

void TypeChart::set(Type attacker, Type defender, float multiplier) {
    chart_[static_cast<size_t>(attacker)][static_cast<size_t>(defender)] = multiplier;
}

float TypeChart::multiplier(Type attacker, Type defender) const {
    return chart_[static_cast<size_t>(attacker)][static_cast<size_t>(defender)];
}

float TypeChart::effectiveness(Type atk, Type def1, Type def2) const {
    float m = multiplier(atk, def1);
    if (def2 != def1) {
        m *= multiplier(atk, def2);
    }
    return m;
}

} // namespace engine
