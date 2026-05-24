#pragma once

#include "engine/effect.hpp"
#include "engine/types.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace engine {

enum class MoveCategory { Physical, Special, Status };

MoveCategory categoryFromString(std::string_view s);
std::string_view categoryName(MoveCategory c);

struct Move {
    std::string name;
    Type type = Type::Normal;
    MoveCategory category = MoveCategory::Physical;
    int power = 0;
    int accuracy = 100;
    int priority = 0; // -7..+5
    std::vector<EffectPtr> effects;

    Move() = default;
    Move(const Move&) = delete;
    Move& operator=(const Move&) = delete;
    Move(Move&&) = default;
    Move& operator=(Move&&) = default;
};

} // namespace engine
