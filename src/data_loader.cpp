#include "engine/data_loader.hpp"

#include "engine/effects/apply_status.hpp"
#include "engine/effects/damage.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace fs = std::filesystem;
using nlohmann::json;

namespace engine {

EffectPtr makeEffectFromJson(const json &j) {
  const std::string kind = j.at("kind").get<std::string>();
  if (kind == "Damage")
    return std::make_unique<DamageEffect>();
  if (kind == "ApplyStatus")
    return std::make_unique<ApplyStatusEffect>(statusFromString(j.at("status").get<std::string>()));
  throw std::invalid_argument("makeEffectFromJson: unknown effect '" + kind + "'");
}

namespace {

json readJsonFile(const fs::path &p) {
  std::ifstream f(p);
  if (!f)
    throw std::runtime_error("Cannot open " + p.string());
  json j;
  f >> j;
  return j;
}

// Sort filenames so catalog indices are stable across runs.
// FFI clients (Rust) cache ids at startup; same data must yield same ids.
std::vector<fs::path> sortedJsonFiles(const fs::path &dir) {
  std::vector<fs::path> files;
  for (auto &entry : fs::directory_iterator(dir)) {
    if (entry.path().extension() == ".json")
      files.push_back(entry.path());
  }
  std::sort(files.begin(), files.end());
  return files;
}

} // namespace

void DataLoader::loadAll(const std::string &dataDir) {
  fs::path root(dataDir);
  if (!fs::exists(root))
    throw std::runtime_error("Data directory not found: " + dataDir);

  loadTypes((root / "types.json").string());
  loadMoves((root / "moves").string());
  loadSpecies((root / "pokemon").string());
}

void DataLoader::loadTypes(const std::string &path) {
  json j = readJsonFile(path);

  for (int a = 0; a < TypeCount; ++a) {
    for (int d = 0; d < TypeCount; ++d) {
      typeChart_.set(static_cast<Type>(a), static_cast<Type>(d), 1.0f);
    }
  }

  for (auto &[attackerStr, row] : j.items()) {
    if (!attackerStr.empty() && attackerStr[0] == '_')
      continue;
    Type attacker = typeFromString(attackerStr);
    for (auto &[defenderStr, mul] : row.items()) {
      if (!defenderStr.empty() && defenderStr[0] == '_')
        continue;
      typeChart_.set(attacker, typeFromString(defenderStr), mul.get<float>());
    }
  }
}

void DataLoader::loadMoves(const std::string &dir) {
  if (!fs::exists(dir))
    return;

  for (const auto &path : sortedJsonFiles(dir)) {
    json j = readJsonFile(path);

    Move m;
    m.name = j.at("name").get<std::string>();
    m.type = typeFromString(j.at("type").get<std::string>());
    m.category = categoryFromString(j.at("category").get<std::string>());
    m.power = j.value("power", 0);
    m.accuracy = j.value("accuracy", 100);
    m.priority = j.value("priority", 0);

    if (j.contains("effects")) {
      for (const auto &e : j.at("effects")) {
        m.effects.push_back(makeEffectFromJson(e));
      }
    }

    if (move_name_to_id_.count(m.name) > 0) {
      throw std::runtime_error("Duplicate move name: " + m.name);
    }
    move_name_to_id_.emplace(m.name, static_cast<int>(moves_.size()));
    moves_.push_back(std::move(m));
  }
}

void DataLoader::loadSpecies(const std::string &dir) {
  if (!fs::exists(dir))
    return;

  for (const auto &path : sortedJsonFiles(dir)) {
    json j = readJsonFile(path);

    Species s;
    s.id = j.at("id").get<std::string>();
    s.displayName = j.at("displayName").get<std::string>();

    const auto &bs = j.at("baseStats");
    s.baseStats.hp = bs.at("hp").get<int>();
    s.baseStats.atk = bs.at("atk").get<int>();
    s.baseStats.def = bs.at("def").get<int>();
    s.baseStats.specAtk = bs.at("specAtk").get<int>();
    s.baseStats.specDef = bs.at("specDef").get<int>();
    s.baseStats.speed = bs.at("speed").get<int>();

    s.type1 = typeFromString(j.at("type1").get<std::string>());
    s.type2 = j.contains("type2") ? typeFromString(j.at("type2").get<std::string>()) : s.type1;
    s.ability = j.value("ability", "");

    for (const auto &mv : j.at("movepool")) {
      s.movepool.push_back(mv.get<std::string>());
    }

    if (species_id_to_index_.count(s.id) > 0) {
      throw std::runtime_error("Duplicate species id: " + s.id);
    }
    species_id_to_index_.emplace(s.id, static_cast<int>(species_.size()));
    species_.push_back(std::move(s));
  }

  for (const auto &sp : species_) {
    for (const auto &mv : sp.movepool) {
      if (findMoveId(mv) < 0) {
        std::ostringstream oss;
        oss << "Species '" << sp.id << "' references unknown move '" << mv << "'";
        throw std::runtime_error(oss.str());
      }
    }
  }
}

const Move &DataLoader::moveByIndex(int id) const {
  if (!isValidMoveId(id))
    throw std::out_of_range("Invalid move id: " + std::to_string(id));
  return moves_[static_cast<size_t>(id)];
}

const Species &DataLoader::speciesByIndex(int id) const {
  if (!isValidSpeciesId(id))
    throw std::out_of_range("Invalid species id: " + std::to_string(id));
  return species_[static_cast<size_t>(id)];
}

int DataLoader::findMoveId(const std::string &name) const {
  auto it = move_name_to_id_.find(name);
  return (it == move_name_to_id_.end()) ? -1 : it->second;
}

int DataLoader::findSpeciesId(const std::string &id) const {
  auto it = species_id_to_index_.find(id);
  return (it == species_id_to_index_.end()) ? -1 : it->second;
}

} // namespace engine
