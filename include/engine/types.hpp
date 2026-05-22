#pragma once

#include <array>
#include <string_view>

namespace engine {

// Stable ordering. Do not reorder without regenerating data files.
enum class Type : int {
    Normal = 0,
    Fire,
    Water,
    Electric,
    Grass,
    Ice,
    Fighting,
    Poison,
    Ground,
    Flying,
    Psychic,
    Bug,
    Rock,
    Ghost,
    Dragon,
    Dark,
    Steel,
    Fairy,
    Count
};

constexpr int TypeCount = static_cast<int>(Type::Count);

std::string_view typeName(Type t);
Type typeFromString(std::string_view s);

class TypeChart {
public:
    void set(Type attacker, Type defender, float multiplier);
    float multiplier(Type attacker, Type defender) const;

    // Combined multiplier against a dual-type defender (mul1 * mul2).
    float effectiveness(Type attackerType, Type defenderType1, Type defenderType2) const;

private:
    std::array<std::array<float, TypeCount>, TypeCount> chart_ {};
};

} // namespace engine
