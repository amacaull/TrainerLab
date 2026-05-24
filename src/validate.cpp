#include "engine/validate.hpp"

#include "engine/battle_state.hpp"
#include "engine/data_loader.hpp"

#include <sstream>
#include <stdexcept>

namespace engine {

namespace {

[[noreturn]] void fail(const std::string& msg) {
    throw std::invalid_argument("validateState: " + msg);
}

void validatePokemon(const BattlePokemon& p, const DataLoader& data, int side, int slot) {
    std::ostringstream where;
    where << "side " << side << " slot " << slot;

    if (!data.isValidSpeciesId(p.species_id)) {
        fail(where.str() + ": invalid species_id " + std::to_string(p.species_id)
             + " (catalog size: " + std::to_string(data.speciesCount()) + ")");
    }
    if (p.level < 1 || p.level > 100) {
        fail(where.str() + ": level out of range [1, 100], got " + std::to_string(p.level));
    }
    if (p.currentHp < 0) {
        fail(where.str() + ": currentHp negative (" + std::to_string(p.currentHp) + ")");
    }
    if (p.currentHp > p.stats.hp) {
        fail(where.str() + ": currentHp (" + std::to_string(p.currentHp)
             + ") exceeds stats.hp (" + std::to_string(p.stats.hp) + ")");
    }
    for (int k = 0; k < kMaxMovesPerPokemon; ++k) {
        int mid = p.move_ids[static_cast<size_t>(k)];
        if (mid == kNoMove) continue;
        if (!data.isValidMoveId(mid)) {
            fail(where.str() + ": move_ids[" + std::to_string(k) + "] invalid ("
                 + std::to_string(mid) + "), catalog size: " + std::to_string(data.moveCount()));
        }
    }
}

} // namespace

void validateState(const BattleState& state, const DataLoader& data) {
    for (int side = 0; side < kSideCount; ++side) {
        int size = state.team_size[static_cast<size_t>(side)];
        if (size < 1 || size > kTeamSize) {
            fail("side " + std::to_string(side)
                 + ": team_size out of range [1, " + std::to_string(kTeamSize)
                 + "], got " + std::to_string(size));
        }

        int active = state.activeIndex[static_cast<size_t>(side)];
        if (active < 0 || active >= size) {
            fail("side " + std::to_string(side)
                 + ": activeIndex (" + std::to_string(active)
                 + ") out of bounds for team_size " + std::to_string(size));
        }

        for (int slot = 0; slot < size; ++slot) {
            validatePokemon(state.teams[static_cast<size_t>(side)][static_cast<size_t>(slot)],
                            data, side, slot);
        }
    }

    if (state.turn < 0) {
        fail("turn negative (" + std::to_string(state.turn) + ")");
    }
}

} // namespace engine
