#include "engine/model/pokemon.hpp"

#include <stdexcept>

namespace engine {
Stats computeSpeciesStats(const Species &sp, int level) {
  const Nature *nature = natureByName(sp.nature);
  if (nature == nullptr)
    throw std::runtime_error("Species '" + sp.id + "' has unknown nature '" + sp.nature + "'");
  return computeStats(sp.baseStats, level, *nature, sp.evs);
}
} // namespace engine
