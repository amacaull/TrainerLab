#include "engine/data_loader.hpp"

#include "engine/ability.hpp"
#include "engine/effects/apply_status.hpp"
#include "engine/effects/clear_hazards.hpp"
#include "engine/effects/damage.hpp"
#include "engine/effects/flinch.hpp"
#include "engine/effects/force_switch.hpp"
#include "engine/effects/pivot.hpp"
#include "engine/effects/protect.hpp"
#include "engine/effects/recoil.hpp"
#include "engine/effects/recovery.hpp"
#include "engine/effects/secondary.hpp"
#include "engine/effects/set_hazard.hpp"
#include "engine/effects/set_weather.hpp"
#include "engine/effects/stat_change.hpp"
#include "engine/field.hpp"
#include "engine/item.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace fs = std::filesystem;
using nlohmann::json;

namespace engine {

namespace {

StatIndex statIndexFromString(const std::string &s) {
  if (s == "Atk")
    return StatIndex::Atk;
  if (s == "Def")
    return StatIndex::Def;
  if (s == "SpA")
    return StatIndex::SpA;
  if (s == "SpD")
    return StatIndex::SpD;
  if (s == "Spe")
    return StatIndex::Spe;
  if (s == "Accuracy")
    return StatIndex::Accuracy;
  if (s == "Evasion")
    return StatIndex::Evasion;
  throw std::invalid_argument("statIndexFromString: unknown stat '" + s + "'");
}

} // namespace

EffectPtr makeEffectFromJson(const json &j) {
  const std::string kind = j.at("kind").get<std::string>();
  if (kind == "Damage")
    return std::make_unique<DamageEffect>();
  if (kind == "ApplyStatus")
    return std::make_unique<ApplyStatusEffect>(statusFromString(j.at("status").get<std::string>()));
  if (kind == "StatChange") {
    StatIndex stat = statIndexFromString(j.at("stat").get<std::string>());
    int delta = j.at("delta").get<int>();
    bool affectsUser = j.value("target", std::string("user")) == "user";
    return std::make_unique<StatChangeEffect>(stat, delta, affectsUser);
  }
  if (kind == "Pivot")
    return std::make_unique<PivotEffect>();
  if (kind == "SetWeather")
    return std::make_unique<SetWeatherEffect>(
        weatherFromString(j.at("weather").get<std::string>()));
  if (kind == "SetHazard")
    return std::make_unique<SetHazardEffect>(hazardFromString(j.at("hazard").get<std::string>()));
  if (kind == "ClearHazards")
    return std::make_unique<ClearHazardsEffect>(j.value("scope", std::string("user")) == "both");
  if (kind == "Flinch")
    return std::make_unique<FlinchEffect>();
  if (kind == "Recoil")
    return std::make_unique<RecoilEffect>(j.at("denominator").get<int>());
  if (kind == "Recovery")
    return std::make_unique<RecoveryEffect>(j.at("denominator").get<int>());
  if (kind == "Roost")
    return std::make_unique<RoostEffect>();
  if (kind == "Rest")
    return std::make_unique<RestEffect>();
  if (kind == "ForceSwitch")
    return std::make_unique<ForceSwitchEffect>();
  if (kind == "Protect")
    return std::make_unique<ProtectEffect>();
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
    m.pp = j.at("pp").get<int>(); // mandatory: every move burns PP (ADR #35)
    if (m.pp < 1 || m.pp > 64) {
      throw std::runtime_error("Move '" + m.name + "' has invalid pp " + std::to_string(m.pp));
    }
    m.makesContact = j.value("contact", false);
    m.highCrit = j.value("highCrit", false);
    m.bypassesProtect = j.value("bypassesProtect", false);
    m.hitsDig = j.value("hitsDig", false);
    m.solarCharge = j.value("solarCharge", false);
    // Protect blocks moves aimed at the protected Pokemon (all damaging moves
    // and foe-targeting status moves). Self/field moves (hazards, weather,
    // recovery, self-boosts) pass through: flag them with "selfOrField": true.
    bool selfOrField = j.value("selfOrField", false);
    m.blockedByProtect = !selfOrField;
    const std::string twoTurn = j.value("twoTurn", std::string(""));
    m.twoTurn = twoTurn == "charge" ? TwoTurn::Charge
                : twoTurn == "fly"  ? TwoTurn::Fly
                : twoTurn == "dig"  ? TwoTurn::Dig
                                    : TwoTurn::None;

    if (j.contains("effects")) {
      for (const auto &e : j.at("effects")) {
        EffectPtr effect = makeEffectFromJson(e);
        // "chance": 30 wraps the effect as a 30% secondary (ADR #26).
        if (e.contains("chance")) {
          float p = e.at("chance").get<float>() / 100.0f;
          effect = std::make_unique<SecondaryEffect>(p, std::move(effect));
        }
        m.effects.push_back(std::move(effect));
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
    s.nature = j.value("nature", std::string("Sérieux"));
    if (natureByName(s.nature) == nullptr) {
      throw std::runtime_error("Species '" + s.id + "' has unknown nature '" + s.nature + "'");
    }
    if (j.contains("evs")) {
      const auto &ev = j.at("evs");
      s.evs.hp = ev.value("hp", 0);
      s.evs.atk = ev.value("atk", 0);
      s.evs.def = ev.value("def", 0);
      s.evs.specAtk = ev.value("specAtk", 0);
      s.evs.specDef = ev.value("specDef", 0);
      s.evs.speed = ev.value("speed", 0);
      int total = 0;
      for (int v : {s.evs.hp, s.evs.atk, s.evs.def, s.evs.specAtk, s.evs.specDef, s.evs.speed}) {
        if (v < 0 || v > 252)
          throw std::runtime_error("Species '" + s.id + "' has an EV outside [0, 252]");
        total += v;
      }
      if (total > 510)
        throw std::runtime_error("Species '" + s.id + "' has EV total " + std::to_string(total) +
                                 " > 510");
    }
    s.weightKg = j.value("poids", 0.0);
    if (s.weightKg < 0.0) {
      throw std::runtime_error("Species '" + s.id + "' has negative weight");
    }
    s.item = j.value("objet", std::string(""));
    if (!s.item.empty() && findItemIdByName(s.item) < 0) {
      throw std::runtime_error("Species '" + s.id + "' references unknown item '" + s.item + "'");
    }

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
    if (!sp.ability.empty() && abilityByName(sp.ability) == nullptr) {
      std::ostringstream oss;
      oss << "Species '" << sp.id << "' references unknown ability '" << sp.ability << "'";
      throw std::runtime_error(oss.str());
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

int DataLoader::findItemId(const std::string &name) const { return findItemIdByName(name); }

bool DataLoader::isValidItemId(int id) const { return id >= 0 && id < itemCount(); }

} // namespace engine
