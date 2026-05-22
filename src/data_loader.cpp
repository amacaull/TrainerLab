#include "engine/data_loader.hpp"

#include "engine/effects/damage.hpp"
#include "engine/effects/effect.hpp"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace fs = std::filesystem;
using nlohmann::json;

namespace engine {

// See TODO in damage.cpp.
const TypeChart* g_typeChart = nullptr;

// Effect factory. Add a case here when adding a new Effect type.
EffectPtr makeEffectFromJson(const json& j) {
    const std::string kind = j.at("kind").get<std::string>();
    if (kind == "Damage") return std::make_unique<DamageEffect>();
    throw std::invalid_argument("makeEffectFromJson: unknown effect '" + kind + "'");
}

namespace {

json readJsonFile(const fs::path& p) {
    std::ifstream f(p);
    if (!f) throw std::runtime_error("Cannot open " + p.string());
    json j;
    f >> j;
    return j;
}

} // namespace

void DataLoader::loadAll(const std::string& dataDir) {
    fs::path root(dataDir);
    if (!fs::exists(root)) throw std::runtime_error("Data directory not found: " + dataDir);

    loadTypes((root / "types.json").string());
    loadMoves((root / "moves").string());
    loadSpecies((root / "pokemon").string());
    g_typeChart = &typeChart_;
}

void DataLoader::loadTypes(const std::string& path) {
    json j = readJsonFile(path);

    // Default to 1.0x for every (attacker, defender) pair.
    for (int a = 0; a < TypeCount; ++a) {
        for (int d = 0; d < TypeCount; ++d) {
            typeChart_.set(static_cast<Type>(a), static_cast<Type>(d), 1.0f);
        }
    }

    // Keys starting with '_' are comments (e.g. "_comment").
    for (auto& [attackerStr, row] : j.items()) {
        if (!attackerStr.empty() && attackerStr[0] == '_') continue;
        Type attacker = typeFromString(attackerStr);
        for (auto& [defenderStr, mul] : row.items()) {
            if (!defenderStr.empty() && defenderStr[0] == '_') continue;
            Type defender = typeFromString(defenderStr);
            typeChart_.set(attacker, defender, mul.get<float>());
        }
    }
}

void DataLoader::loadMoves(const std::string& dir) {
    if (!fs::exists(dir)) return;

    for (auto& entry : fs::directory_iterator(dir)) {
        if (entry.path().extension() != ".json") continue;
        json j = readJsonFile(entry.path());

        Move m;
        m.name     = j.at("name").get<std::string>();
        m.type     = typeFromString(j.at("type").get<std::string>());
        m.category = categoryFromString(j.at("category").get<std::string>());
        m.power    = j.value("power", 0);
        m.accuracy = j.value("accuracy", 100);
        m.priority = j.value("priority", 0);

        if (j.contains("effects")) {
            for (const auto& e : j.at("effects")) {
                m.effects.push_back(makeEffectFromJson(e));
            }
        }
        moves_.emplace(m.name, std::move(m));
    }
}

void DataLoader::loadSpecies(const std::string& dir) {
    if (!fs::exists(dir)) return;

    for (auto& entry : fs::directory_iterator(dir)) {
        if (entry.path().extension() != ".json") continue;
        json j = readJsonFile(entry.path());

        Species s;
        s.id          = j.at("id").get<std::string>();
        s.displayName = j.at("displayName").get<std::string>();

        const auto& bs = j.at("baseStats");
        s.baseStats.hp      = bs.at("hp").get<int>();
        s.baseStats.atk     = bs.at("atk").get<int>();
        s.baseStats.def     = bs.at("def").get<int>();
        s.baseStats.specAtk = bs.at("specAtk").get<int>();
        s.baseStats.specDef = bs.at("specDef").get<int>();
        s.baseStats.speed   = bs.at("speed").get<int>();

        s.type1 = typeFromString(j.at("type1").get<std::string>());
        s.type2 = j.contains("type2") ? typeFromString(j.at("type2").get<std::string>()) : s.type1;
        s.ability = j.value("ability", "");

        for (const auto& mv : j.at("movepool")) {
            s.movepool.push_back(mv.get<std::string>());
        }
        species_.emplace(s.id, std::move(s));
    }

    // Validate: every move in every movepool must exist.
    for (const auto& [id, sp] : species_) {
        for (const auto& mv : sp.movepool) {
            if (!hasMove(mv)) {
                std::ostringstream oss;
                oss << "Species '" << id << "' references unknown move '" << mv << "'";
                throw std::runtime_error(oss.str());
            }
        }
    }
}

const Move& DataLoader::move(const std::string& name) const {
    auto it = moves_.find(name);
    if (it == moves_.end()) throw std::out_of_range("Unknown move: " + name);
    return it->second;
}

const Species& DataLoader::species(const std::string& id) const {
    auto it = species_.find(id);
    if (it == species_.end()) throw std::out_of_range("Unknown species: " + id);
    return it->second;
}

} // namespace engine
