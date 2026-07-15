#include "engine/validate.hpp"

#include <string>

#include "engine/battle_state.hpp"
#include "engine/data_loader.hpp"
#include "engine/status.hpp"

#include <sstream>
#include <stdexcept>

namespace engine {

namespace {

[[noreturn]] void fail(const std::string &msg) {
  throw std::invalid_argument("validateState: " + msg);
}

void validatePokemon(const BattlePokemon &p, const DataLoader &data, int side, int slot) {
  std::ostringstream where;
  where << "side " << side << " slot " << slot;

  if (!data.isValidSpeciesId(p.species_id)) {
    fail(where.str() + ": invalid species_id " + std::to_string(p.species_id) +
         " (catalog size: " + std::to_string(data.speciesCount()) + ")");
  }
  if (p.level < 1 || p.level > 100) {
    fail(where.str() + ": level out of range [1, 100], got " + std::to_string(p.level));
  }
  if (p.currentHp < 0) {
    fail(where.str() + ": currentHp negative (" + std::to_string(p.currentHp) + ")");
  }
  if (p.currentHp > p.stats.hp) {
    fail(where.str() + ": currentHp (" + std::to_string(p.currentHp) + ") exceeds stats.hp (" +
         std::to_string(p.stats.hp) + ")");
  }
  for (int k = 0; k < kMaxMovesPerPokemon; ++k) {
    int mid = p.move_ids[static_cast<size_t>(k)];
    if (mid == kNoMove)
      continue;
    if (!data.isValidMoveId(mid)) {
      fail(where.str() + ": move_ids[" + std::to_string(k) + "] invalid (" + std::to_string(mid) +
           "), catalog size: " + std::to_string(data.moveCount()));
    }
  }

  int statusVal = static_cast<int>(p.status);
  if (statusVal < 0 || statusVal >= StatusCount) {
    fail(where.str() + ": invalid status value " + std::to_string(statusVal) + " (must be in [0, " +
         std::to_string(StatusCount) + "))");
  }
  if (p.status_turns < 0) {
    fail(where.str() + ": status_turns negative (" + std::to_string(p.status_turns) + ")");
  }

  if (p.sleep_self_inflicted != 0 && p.status != Status::Sleep) {
    fail(where.str() + ": sleep_self_inflicted set without Sleep status");
  }
  if (p.flinched < 0 || p.flinched > 1 || p.roosted < 0 || p.roosted > 1 || p.protected_now < 0 ||
      p.protected_now > 1) {
    fail(where.str() + ": volatile flags must be 0 or 1");
  }
  if (p.protect_chain < 0) {
    fail(where.str() + ": protect_chain negative");
  }
  if (p.charging_move_id != kNoMove && !data.isValidMoveId(p.charging_move_id)) {
    fail(where.str() + ": charging_move_id invalid (" + std::to_string(p.charging_move_id) + ")");
  }
  if (p.invulnerable_state < 0 || p.invulnerable_state > 2) {
    fail(where.str() + ": invulnerable_state out of range [0, 2]");
  }
  if (p.invulnerable_state != 0 && p.charging_move_id == kNoMove) {
    fail(where.str() + ": invulnerable without a charging move");
  }

  for (int k = 0; k < kStatStageCount; ++k) {
    int stage = p.stat_stages[static_cast<size_t>(k)];
    if (stage < kMinStage || stage > kMaxStage) {
      fail(where.str() + ": stat_stages[" + std::to_string(k) + "] out of range [" +
           std::to_string(kMinStage) + ", " + std::to_string(kMaxStage) + "], got " +
           std::to_string(stage));
    }
  }
}

} // namespace

void validateState(const BattleState &state, const DataLoader &data) {
  if (state.weather < Weather::None || state.weather >= Weather::Count)
    fail("weather has an invalid enum value");
  if (state.weather_turns_left < 0)
    fail("weather_turns_left is negative");
  if (state.weather == Weather::None && state.weather_turns_left != 0)
    fail("weather_turns_left must be 0 when weather is None");
  if (state.weather != Weather::None && state.weather_turns_left > kWeatherDuration)
    fail("weather_turns_left exceeds the maximum duration");

  for (int side = 0; side < kSideCount; ++side) {
    const SideHazards &hz = state.hazards[static_cast<size_t>(side)];
    if (hz.stealth_rock < 0 || hz.stealth_rock > kMaxStealthRock)
      fail("stealth_rock layers out of range for side " + std::to_string(side));
    if (hz.spikes < 0 || hz.spikes > kMaxSpikes)
      fail("spikes layers out of range for side " + std::to_string(side));
    if (hz.toxic_spikes < 0 || hz.toxic_spikes > kMaxToxicSpikes)
      fail("toxic_spikes layers out of range for side " + std::to_string(side));
  }

  for (int side = 0; side < kSideCount; ++side) {
    int size = state.team_size[static_cast<size_t>(side)];
    if (size < 1 || size > kTeamSize) {
      fail("side " + std::to_string(side) + ": team_size out of range [1, " +
           std::to_string(kTeamSize) + "], got " + std::to_string(size));
    }

    int active = state.activeIndex[static_cast<size_t>(side)];
    if (active < 0 || active >= size) {
      fail("side " + std::to_string(side) + ": activeIndex (" + std::to_string(active) +
           ") out of bounds for team_size " + std::to_string(size));
    }

    for (int slot = 0; slot < size; ++slot) {
      validatePokemon(state.teams[static_cast<size_t>(side)][static_cast<size_t>(slot)], data, side,
                      slot);
    }
  }

  if (state.turn < 0) {
    fail("turn negative (" + std::to_string(state.turn) + ")");
  }
}

} // namespace engine
