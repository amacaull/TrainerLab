#include "search/features.hpp"

#include <algorithm>
#include <array>

namespace search {
namespace {
using engine::BattlePokemon;
using engine::BattleState;

constexpr std::array kStatuses{"burn", "poison", "toxic", "paralysis", "sleep", "freeze"};
constexpr std::array kStages{"atk", "def", "spa", "spd", "spe", "accuracy", "evasion"};
constexpr std::array kWeathers{"rain", "sun", "sand", "snow", "strong_winds"};
constexpr int kSpeedStage = static_cast<int>(engine::StatIndex::Spe);

// The columns before the two sides, then each side's block: the offsets of features.py.
constexpr int kReplacement = 0, kTurn = 1, kMyFaster = 2, kMyBestDamage = 3, kFoeBestDamage = 4;
constexpr int kWeather = 5; // 5 columns, rain first
constexpr int kTerrain = kWeather + static_cast<int>(kWeathers.size());
constexpr int kSides = kTerrain + 1;
// Within a side: active HP, statuses, stages, then the team, the hazards, the species.
constexpr int kActiveHp = 0;
constexpr int kStatus = 1;
constexpr int kStage = kStatus + static_cast<int>(kStatuses.size());
constexpr int kAlive = kStage + static_cast<int>(kStages.size());
constexpr int kHpTotal = kAlive + 1, kHidden = kAlive + 2;
constexpr int kStealthRock = kAlive + 3, kSpikes = kAlive + 4, kToxicSpikes = kAlive + 5;
constexpr int kAuroraVeil = kAlive + 6;
constexpr int kActiveIs = kAuroraVeil + 1; // one column per species, then the HP of each species

double staged(int value, int stage) {
  return stage >= 0 ? static_cast<double>(value * (2 + stage)) / 2
                    : static_cast<double>(value * 2) / (2 - stage);
}

size_t at(int i) { return static_cast<size_t>(i); }
} // namespace

Features::Features(const engine::DataLoader &data, const engine::BattleEngine &rules)
    : rules_(rules), species_(data.speciesCount()), choiceScarf_(data.findItemId("ChoiceScarf")) {
  names_ = {"replacement", "turn", "my_faster", "my_best_damage", "foe_best_damage"};
  for (const char *w : kWeathers)
    names_.push_back(std::string("weather_") + w);
  names_.push_back("terrain_electric");
  for (std::string p : {"my", "foe"}) {
    names_.push_back(p + "_active_hp");
    for (const char *s : kStatuses)
      names_.push_back(p + "_active_" + s);
    for (const char *s : kStages)
      names_.push_back(p + "_active_stage_" + s);
    for (const char *s : {"_alive", "_hp_total", "_hidden", "_stealth_rock", "_spikes",
                          "_toxic_spikes", "_aurora_veil"})
      names_.push_back(p + s);
    for (int i = 0; i < species_; ++i)
      names_.push_back(p + "_active_is_" + data.speciesByIndex(i).id);
    for (int i = 0; i < species_; ++i)
      names_.push_back(p + "_hp_of_" + data.speciesByIndex(i).id);
  }
}

double Features::effectiveSpeed(const BattlePokemon &pokemon) const {
  double speed = staged(pokemon.stats.speed, pokemon.stat_stages[at(kSpeedStage)]);
  if (pokemon.item_id == choiceScarf_ && pokemon.item_id != engine::kNoItem &&
      !pokemon.item_consumed)
    speed *= 1.5;
  return pokemon.status == engine::Status::Paralysis ? speed / 2 : speed;
}

double Features::bestDamage(const BattleState &view, int side) const {
  const BattlePokemon &foe = view.active(1 - side);
  if (foe.isFainted())
    return 0.0;
  const BattlePokemon &attacker = view.active(side);
  double best = 0.0;
  bool any = false;
  for (int slot = 0; slot < engine::kMaxMovesPerPokemon; ++slot) {
    if (attacker.move_ids[at(slot)] == engine::kNoMove)
      continue;
    const double mean = rules_.expectedDamage(view, side, slot).mean;
    best = any ? std::max(best, mean) : mean;
    any = true;
  }
  return best / foe.currentHp;
}

void Features::compute(const BattleState &view, int side, float *out) const {
  std::fill(out, out + names_.size(), 0.0f);
  auto set = [out](int column, double value) { out[at(column)] = static_cast<float>(value); };
  const BattlePokemon &me = view.active(side);
  const BattlePokemon &foe = view.active(1 - side);

  set(kReplacement, me.isFainted());
  set(kTurn, view.turn / 100.0);
  const double lead = effectiveSpeed(me) - effectiveSpeed(foe);
  set(kMyFaster, lead > 0 ? 1.0 : lead < 0 ? 0.0 : 0.5);
  set(kMyBestDamage, bestDamage(view, side));
  set(kFoeBestDamage, bestDamage(view, 1 - side));
  if (view.weather != engine::Weather::None)
    set(kWeather + static_cast<int>(view.weather) - 1, 1.0);
  set(kTerrain, view.terrain != engine::Terrain::None);

  const int block = kActiveIs + 2 * species_;
  for (int p = 0; p < 2; ++p) {
    const int s = p == 0 ? side : 1 - side;
    const int base = kSides + p * block;
    const BattlePokemon &active = view.active(s);
    set(base + kActiveHp, static_cast<double>(active.currentHp) / active.stats.hp);
    if (active.status != engine::Status::None)
      set(base + kStatus + static_cast<int>(active.status) - 1, 1.0);
    for (int stat = 0; stat < static_cast<int>(kStages.size()); ++stat)
      set(base + kStage + stat, active.stat_stages[at(stat)] / 6.0);
    set(base + kActiveIs + active.species_id, 1.0);

    int alive = 0, hidden = 0;
    double hpTotal = 0.0;
    for (int i = 0; i < view.team_size[at(s)]; ++i) {
      const BattlePokemon &pokemon = view.teams[at(s)][at(i)];
      if (pokemon.isEmpty()) {
        ++hidden;
        continue;
      }
      const double share = static_cast<double>(pokemon.currentHp) / pokemon.stats.hp;
      alive += share > 0;
      hpTotal += share;
      set(base + kActiveIs + species_ + pokemon.species_id, share);
    }
    set(base + kAlive, static_cast<double>(alive + hidden) / engine::kTeamSize);
    set(base + kHpTotal, (hpTotal + hidden) / engine::kTeamSize);
    set(base + kHidden, static_cast<double>(hidden) / engine::kTeamSize);
    const engine::SideHazards &hazards = view.hazards[at(s)];
    set(base + kStealthRock, hazards.stealth_rock);
    set(base + kSpikes, hazards.spikes / 3.0);
    set(base + kToxicSpikes, hazards.toxic_spikes / 2.0);
    set(base + kAuroraVeil, view.aurora_veil_turns[at(s)] / 8.0);
  }
}
} // namespace search
