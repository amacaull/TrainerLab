#include "search/network.hpp"

#include "engine/core/observation.hpp"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <stdexcept>

namespace search {
namespace {
constexpr int kTeam = engine::kTeamSize;
constexpr int kMoves = engine::kMaxMovesPerPokemon;
constexpr float kMasked = -1e9f;

size_t at(int i) { return static_cast<size_t>(i); }

const Network::Tensor &find(const std::map<std::string, Network::Tensor> &weights,
                            const std::string &name) {
  const auto it = weights.find(name);
  if (it == weights.end())
    throw std::invalid_argument("Network: missing weight " + name);
  return it->second;
}

int column(const std::vector<std::string> &names, const std::string &name) {
  const auto it = std::find(names.begin(), names.end(), name);
  if (it == names.end())
    throw std::invalid_argument("Network: no column " + name);
  return static_cast<int>(std::distance(names.begin(), it));
}
} // namespace

namespace {
// The dot product of n floats, in eight running sums: the compiler turns them into vector
// instructions, which a single running sum (an order it must keep) forbids.
float dot(const float *w, const float *x, int n) {
  float acc[8] = {};
  int i = 0;
  for (; i + 8 <= n; i += 8)
    for (int k = 0; k < 8; ++k)
      acc[k] += w[i + k] * x[i + k];
  float sum = ((acc[0] + acc[1]) + (acc[2] + acc[3])) + ((acc[4] + acc[5]) + (acc[6] + acc[7]));
  for (; i < n; ++i)
    sum += w[i] * x[i];
  return sum;
}
} // namespace

void Network::Linear::apply(const float *x, float *y, bool relu) const {
  for (int o = 0; o < out; ++o) {
    const float sum = bias[at(o)] + dot(weight.data() + at(o) * at(in), x, in);
    y[o] = relu ? std::max(sum, 0.0f) : sum;
  }
}

void Network::Linear::partial(const float *x, int from, int count, float *y) const {
  for (int o = 0; o < out; ++o)
    y[o] = dot(weight.data() + at(o) * at(in) + at(from), x, count);
}

Network::Network(const Encoding &encoding, const std::map<std::string, Tensor> &weights)
    : encoding_(encoding) {
  auto linear = [&](const std::string &name) {
    const std::string weightName = name + ".weight", biasName = name + ".bias";
    const Tensor &w = find(weights, weightName);
    const Tensor &b = find(weights, biasName);
    if (w.shape.size() != 2 || b.shape.size() != 1 || b.shape[0] != w.shape[0])
      throw std::invalid_argument("Network: bad shape for " + name);
    return Linear{w.shape[1], w.shape[0], w.values, b.values};
  };
  auto embedding = [&](const std::string &name) {
    const std::string weightName = name + ".weight";
    const Tensor &t = find(weights, weightName);
    if (t.shape.size() != 2)
      throw std::invalid_argument("Network: bad shape for " + name);
    return Embedding{t.shape[1], t.values};
  };
  species_ = embedding("species");
  ability_ = embedding("ability");
  item_ = embedding("item");
  move_ = embedding("move");
  pokemon1_ = linear("pokemon.0");
  pokemon2_ = linear("pokemon.2");
  trunk1_ = linear("trunk.0");
  trunk2_ = linear("trunk.2");
  moves1_ = linear("moves.0");
  moves2_ = linear("moves.2");
  switch1_ = linear("switch.0");
  switch2_ = linear("switch.2");
  value_ = linear("value");

  const auto &names = encoding.pokemonNames();
  present_ = column(names, "present");
  active_ = column(names, "active");
  moveColumns_ = column(names, "move1_present");
  moveWidth_ = column(names, "move2_present") - moveColumns_;
  if (pokemon1_.in != static_cast<int>(names.size()) + 7 * species_.dim)
    throw std::invalid_argument("Network: weights made for another encoding");
}

Network::Output Network::evaluate(const engine::BattleState &view, int side,
                                  const float *mask) const {
  const int width = static_cast<int>(encoding_.pokemonNames().size());
  const int fields = static_cast<int>(encoding_.fieldNames().size());
  std::vector<float> pokemon(at(Encoding::kSlots * width));
  std::vector<int32_t> ids(at(Encoding::kSlots * Encoding::kIds));
  std::vector<float> field(at(fields));
  encoding_.encode(view, side, pokemon.data(), ids.data(), field.data());

  // Each Pokemon, read by the same small network.
  const int hidden = pokemon2_.out, e = species_.dim;
  std::vector<float> input(at(pokemon1_.in)), first(at(pokemon1_.out));
  std::vector<float> each(at(Encoding::kSlots * hidden));
  for (int s = 0; s < Encoding::kSlots; ++s) {
    const float *numbers = pokemon.data() + at(s * width);
    const int32_t *id = ids.data() + at(s * Encoding::kIds);
    float *x = std::copy(numbers, numbers + width, input.data());
    x = std::copy_n(species_.row(id[0]), e, x);
    x = std::copy_n(ability_.row(id[1]), e, x);
    x = std::copy_n(item_.row(id[2]), e, x);
    for (int m = 0; m < kMoves; ++m)
      x = std::copy_n(move_.row(id[3 + m]), e, x);
    pokemon1_.apply(input.data(), first.data(), true);
    pokemon2_.apply(first.data(), each.data() + at(s * hidden), true);
  }

  // The trunk: both actives, the mean of each team, the field.
  std::vector<float> trunkIn(at(trunk1_.in), 0.0f);
  for (int p = 0; p < 2; ++p) {
    float *active = trunkIn.data() + at(p * hidden);
    float *mean = trunkIn.data() + at((2 + p) * hidden);
    float present = 0.0f;
    for (int i = 0; i < kTeam; ++i) {
      const int s = p * kTeam + i;
      const float *row = pokemon.data() + at(s * width);
      const float *h = each.data() + at(s * hidden);
      for (int k = 0; k < hidden; ++k) {
        active[k] += h[k] * row[active_];
        mean[k] += h[k] * row[present_];
      }
      present += row[present_];
    }
    for (int k = 0; k < hidden; ++k)
      mean[k] /= std::max(present, 1.0f);
  }
  std::copy(field.begin(), field.end(), trunkIn.begin() + 4 * hidden);
  std::vector<float> t1(at(trunk1_.out)), trunk(at(trunk2_.out));
  trunk1_.apply(trunkIn.data(), t1.data(), true);
  trunk2_.apply(t1.data(), trunk.data(), true);
  const int t = trunk2_.out;

  Output out{};
  // Each move of the active Pokemon, read with the trunk.
  int lead = 0;
  for (int i = 1; i < kTeam; ++i)
    if (pokemon[at(i * width + active_)] > pokemon[at(lead * width + active_)])
      lead = i;
  // A head's first layer reads [trunk, option]: the trunk's share is the same for every option,
  // computed once.
  std::vector<float> fromTrunk(at(moves1_.out)), fromOption(at(moves1_.out));
  std::vector<float> option(at(e + moveWidth_)), head(at(moves1_.out));
  moves1_.partial(trunk.data(), 0, t, fromTrunk.data());
  for (int m = 0; m < kMoves; ++m) {
    float *x = std::copy_n(move_.row(ids[at(lead * Encoding::kIds + 3 + m)]), e, option.data());
    const float *numbers = pokemon.data() + at(lead * width + moveColumns_ + m * moveWidth_);
    std::copy_n(numbers, moveWidth_, x);
    moves1_.partial(option.data(), t, e + moveWidth_, fromOption.data());
    for (int k = 0; k < moves1_.out; ++k)
      head[at(k)] = std::max(moves1_.bias[at(k)] + fromTrunk[at(k)] + fromOption[at(k)], 0.0f);
    moves2_.apply(head.data(), &out.logits[at(m)], false);
  }
  // Each switch, read with the Pokemon it would send in.
  switch1_.partial(trunk.data(), 0, t, fromTrunk.data());
  for (int i = 0; i < kTeam; ++i) {
    switch1_.partial(each.data() + at(i * hidden), t, hidden, fromOption.data());
    for (int k = 0; k < switch1_.out; ++k)
      head[at(k)] = std::max(switch1_.bias[at(k)] + fromTrunk[at(k)] + fromOption[at(k)], 0.0f);
    switch2_.apply(head.data(), &out.logits[at(kMoves + i)], false);
  }
  for (int a = 0; a < Encoding::kActions; ++a)
    if (mask[a] == 0.0f)
      out.logits[at(a)] = kMasked;
  float v = 0.0f;
  value_.apply(trunk.data(), &v, false);
  out.value = std::tanh(v);
  return out;
}
} // namespace search
