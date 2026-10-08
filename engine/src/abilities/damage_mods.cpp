#include "engine/abilities/registration.hpp"

namespace engine {
namespace {
class HugePower final : public Ability {
public:
  const char *name() const override { return "HugePower"; }
  float statMultiplier(StatIndex stat) const override {
    return stat == StatIndex::Atk ? 2.0f : 1.0f;
  }
};

class Technician final : public Ability {
public:
  const char *name() const override { return "Technician"; }
  float damageMultiplier(const Move &move, const BattlePokemon &) const override {
    return (move.power > 0 && move.power <= 60) ? 1.5f : 1.0f;
  }
};

class Adaptability final : public Ability {
public:
  const char *name() const override { return "Adaptability"; }
  float stabMultiplier() const override { return 2.0f; }
};

class ToughClaws final : public Ability {
public:
  const char *name() const override { return "ToughClaws"; }
  float damageMultiplier(const Move &move, const BattlePokemon &) const override {
    return move.makesContact ? 1.3f : 1.0f;
  }
};

class IronFist final : public Ability {
public:
  const char *name() const override { return "IronFist"; }
  float damageMultiplier(const Move &move, const BattlePokemon &) const override {
    return move.punch ? 1.2f : 1.0f;
  }
};

class Sharpness final : public Ability {
public:
  const char *name() const override { return "Sharpness"; }
  float damageMultiplier(const Move &move, const BattlePokemon &) const override {
    return move.slicing ? 1.5f : 1.0f;
  }
};

class Unaware final : public Ability {
public:
  const char *name() const override { return "Unaware"; }
  bool ignoresStages() const override { return true; }
};

class VesselOfRuin final : public Ability {
public:
  const char *name() const override { return "VesselOfRuin"; }
  float opposingSpAMultiplier() const override { return 0.75f; }
};
} // namespace

void registerDamageModAbilities(AbilityTable &table) {
  static const HugePower hugePower;
  static const Technician technician;
  static const Adaptability adaptability;
  static const ToughClaws toughClaws;
  static const IronFist ironFist;
  static const Sharpness sharpness;
  static const Unaware unaware;
  static const VesselOfRuin vesselOfRuin;
  auto add = [&table](const Ability &a) { table.push_back(&a); };
  add(hugePower);
  add(technician);
  add(adaptability);
  add(toughClaws);
  add(ironFist);
  add(sharpness);
  add(unaware);
  add(vesselOfRuin);
}
} // namespace engine
