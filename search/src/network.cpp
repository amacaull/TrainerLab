#include "search/network.hpp"

#include "engine/core/observation.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

#if defined(__aarch64__)
#include <arm_neon.h>
#endif
#if defined(SEARCH_BLAS) && defined(__APPLE__)
#define ACCELERATE_NEW_LAPACK
#include <Accelerate/Accelerate.h>
#elif defined(SEARCH_BLAS)
#include <cblas.h>
#endif
#include <iterator>
#include <span>
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
// Four floats side by side, one vector register: NEON on the M4, SSE on x86. On ARM, each step
// multiplies and adds in one fused instruction (FMA, rounded once): faster, and a hair more
// precise, so the M4 and x86 no longer give the same numbers bit for bit (palier 8.1). Each
// machine still gives a position the same numbers alone or in any batch.
typedef float Vec __attribute__((vector_size(16)));
constexpr int kLanes = 4;
Vec load(const float *p) {
  Vec v;
  std::memcpy(&v, p, sizeof v);
  return v;
}

// acc + w * x, lane by lane: fused on ARM, a multiply then an add elsewhere.
Vec fused(Vec acc, Vec w, float x) {
#if defined(__aarch64__)
  return vfmaq_n_f32(acc, w, x);
#else
  return acc + x * w;
#endif
}

// The running sums kept in registers: 4 rows times 16 outputs on ARM (32 vector registers), times
// 8 elsewhere (16). The blocks only decide which sums are kept together: each output still sums
// its inputs in order, one by one.
#if defined(__aarch64__)
constexpr int kVecs = 4;
#else
constexpr int kVecs = 2;
#endif
constexpr int kBlock = kVecs * kLanes;
constexpr int kRows = 4;

// For R rows of x (`stride` apart) and kBlock outputs, the sums over `count` inputs; w steps
// `out` floats per input, y `out` floats per row.
template <int R>
void block(const float *x, int stride, const float *w, int out, int count, float *y) {
  Vec acc[R][kVecs] = {};
  for (int i = 0; i < count; ++i) {
    const float *wi = w + at(i) * at(out);
    Vec wv[kVecs];
    for (int k = 0; k < kVecs; ++k)
      wv[k] = load(wi + k * kLanes);
    for (int r = 0; r < R; ++r) {
      const float xi = x[at(r) * at(stride) + at(i)];
      for (int k = 0; k < kVecs; ++k)
        acc[r][k] = fused(acc[r][k], wv[k], xi);
    }
  }
  for (int r = 0; r < R; ++r)
    std::memcpy(y + at(r) * at(out), acc[r], sizeof acc[r]);
}

// The same sums, one output at a time: the outputs left over (the heads' single one).
template <int R>
void single(const float *x, int stride, const float *w, int out, int count, float *y) {
  float acc[R] = {};
  for (int i = 0; i < count; ++i)
    for (int r = 0; r < R; ++r)
      acc[r] += x[at(r) * at(stride) + at(i)] * w[at(i) * at(out)];
  for (int r = 0; r < R; ++r)
    y[at(r) * at(out)] = acc[r];
}

template <int R>
void rows(const float *x, int stride, const float *w, int out, int count, float *y) {
  int o = 0;
  for (; o + kBlock <= out; o += kBlock)
    block<R>(x, stride, w + o, out, count, y + o);
  for (; o < out; ++o)
    single<R>(x, stride, w + o, out, count, y + o);
}
} // namespace

void multiply(const float *x, int rowCount, int stride, const float *w, int count, int out,
              float *y) {
  int r = 0;
  for (; r + kRows <= rowCount; r += kRows)
    rows<kRows>(x + at(r) * at(stride), stride, w, out, count, y + at(r) * at(out));
  const float *xr = x + at(r) * at(stride);
  float *yr = y + at(r) * at(out);
  switch (rowCount - r) {
  case 3:
    rows<3>(xr, stride, w, out, count, yr);
    break;
  case 2:
    rows<2>(xr, stride, w, out, count, yr);
    break;
  case 1:
    rows<1>(xr, stride, w, out, count, yr);
    break;
  default:
    break;
  }
}

// Palier 8.2: on the M4, Accelerate's matrix product (its matrix unit) is ten times our kernel's,
// even for one position. Its sums are not ours, in another order: a position may then get numbers
// a hair different in another batch.
const char *Network::backend() {
#if defined(SEARCH_BLAS) && defined(__APPLE__)
  return "Accelerate";
#elif defined(SEARCH_BLAS)
  return "BLAS";
#else
  return "kernel";
#endif
}

void Network::Linear::partial(const float *x, int rowCount, int stride, int from, int count,
                              float *y) const {
  const float *w = weight.data() + at(from) * at(out);
#if defined(SEARCH_BLAS)
  cblas_sgemm(CblasRowMajor, CblasNoTrans, CblasNoTrans, rowCount, out, count, 1.0f, x, stride, w,
              out, 0.0f, y, out);
#else
  multiply(x, rowCount, stride, w, count, out, y);
#endif
}

void Network::Linear::apply(const float *x, int rowCount, float *y, bool relu) const {
  partial(x, rowCount, in, 0, in, y);
  for (int r = 0; r < rowCount; ++r)
    for (int o = 0; o < out; ++o) {
      float &v = y[at(r) * at(out) + at(o)];
      v += bias[at(o)];
      if (relu)
        v = std::max(v, 0.0f);
    }
}

Network::Network(const Encoding &encoding, const std::map<std::string, Tensor> &weights)
    : encoding_(encoding) {
  auto linear = [&](const std::string &name) {
    const std::string weightName = name + ".weight", biasName = name + ".bias";
    const Tensor &w = find(weights, weightName);
    const Tensor &b = find(weights, biasName);
    if (w.shape.size() != 2 || b.shape.size() != 1 || b.shape[0] != w.shape[0])
      throw std::invalid_argument("Network: bad shape for " + name);
    const int in = w.shape[1], out = w.shape[0];
    std::vector<float> transposed(w.values.size());
    for (int o = 0; o < out; ++o)
      for (int i = 0; i < in; ++i)
        transposed[at(i) * at(out) + at(o)] = w.values[at(o) * at(in) + at(i)];
    return Linear{in, out, std::move(transposed), b.values};
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
  known_ = column(names, "known");
  active_ = column(names, "active");
  moveColumns_ = column(names, "move1_present");
  moveWidth_ = column(names, "move2_present") - moveColumns_;
  if (pokemon1_.in != static_cast<int>(names.size()) + 7 * species_.dim)
    throw std::invalid_argument("Network: weights made for another encoding");

  // [an unseen foe, an empty place]: present or not, nothing else; all ids 0.
  const int e = species_.dim, width = static_cast<int>(names.size());
  std::vector<float> input(at(2 * pokemon1_.in), 0.0f), first(at(2 * pokemon1_.out));
  std::vector<float> each(at(2 * pokemon2_.out));
  for (int r = 0; r < 2; ++r) {
    float *row = input.data() + at(r * pokemon1_.in);
    row[present_] = r == 0 ? 1.0f : 0.0f;
    const float *embeddings[] = {species_.row(0), ability_.row(0), item_.row(0), move_.row(0),
                                 move_.row(0),    move_.row(0),    move_.row(0)};
    for (int k = 0; k < Encoding::kIds; ++k)
      std::copy_n(embeddings[k], e, row + width + k * e);
  }
  pokemon1_.apply(input.data(), 2, first.data(), true);
  pokemon2_.apply(first.data(), 2, each.data(), true);
  unseen_.assign(each.begin(), each.begin() + pokemon2_.out);
  empty_.assign(each.begin() + pokemon2_.out, each.end());
}

Network::Output Network::evaluate(const engine::BattleState &view, int side,
                                  const float *mask) const {
  Output out;
  const Query query{&view, side, mask};
  evaluate(std::span<const Query>(&query, 1), &out);
  return out;
}

void Network::evaluate(std::span<const Query> queries, Output *out) const {
  const int b = static_cast<int>(queries.size());
  const int width = static_cast<int>(encoding_.pokemonNames().size());
  const int fields = static_cast<int>(encoding_.fieldNames().size());
  const int slots = b * Encoding::kSlots, hidden = pokemon2_.out, e = species_.dim;
  const int t = trunk2_.out, option = e + moveWidth_;

  // One buffer for the whole batch, kept from call to call: no allocation once warm.
  thread_local std::vector<float> arena;
  thread_local std::vector<int32_t> ids;
  thread_local std::vector<int> knownSlots;
  const int sizes[] = {slots * width,
                       b * fields,
                       slots * pokemon1_.in,
                       slots * pokemon1_.out,
                       slots * hidden,
                       slots * hidden,
                       b * trunk1_.in,
                       b * trunk1_.out,
                       b * t,
                       b * moves1_.out,
                       b * kMoves * option,
                       b * kMoves * moves1_.out,
                       b * kMoves * moves1_.out,
                       b * kMoves,
                       b * switch1_.out,
                       b * kTeam * switch1_.out,
                       b * kTeam * switch1_.out,
                       b * kTeam,
                       b};
  size_t total = 0;
  for (int size : sizes)
    total += at(size);
  arena.resize(total);
  ids.resize(at(slots * Encoding::kIds));
  float *next = arena.data();
  auto take = [&next, &sizes](int k) {
    float *p = next;
    next += sizes[k];
    return p;
  };
  float *pokemon = take(0), *field = take(1), *input = take(2), *first = take(3), *second = take(4),
        *each = take(5);
  float *trunkIn = take(6), *t1 = take(7), *trunk = take(8);
  float *movesTrunk = take(9), *options = take(10), *movesOption = take(11), *movesHead = take(12);
  float *moveLogits = take(13);
  float *switchTrunk = take(14), *switchOption = take(15), *switchHead = take(16);
  float *switchLogits = take(17), *values = take(18);

  for (int q = 0; q < b; ++q)
    encoding_.encode(
        *queries[at(q)].view, queries[at(q)].side, pokemon + at(q * Encoding::kSlots * width),
        ids.data() + at(q * Encoding::kSlots * Encoding::kIds), field + at(q * fields));

  // Each Pokemon, read by the same small network: all the batch's known Pokemon at once, the
  // others (unseen foes, empty places) from what was computed once.
  std::vector<int> &known = knownSlots;
  known.clear();
  for (int s = 0; s < slots; ++s) {
    const float *numbers = pokemon + at(s * width);
    if (numbers[known_] == 0.0f) {
      const std::vector<float> &h = numbers[present_] == 1.0f ? unseen_ : empty_;
      std::copy(h.begin(), h.end(), each + at(s * hidden));
      continue;
    }
    const int32_t *id = ids.data() + at(s * Encoding::kIds);
    float *x = std::copy(numbers, numbers + width, input + known.size() * at(pokemon1_.in));
    x = std::copy_n(species_.row(id[0]), e, x);
    x = std::copy_n(ability_.row(id[1]), e, x);
    x = std::copy_n(item_.row(id[2]), e, x);
    for (int m = 0; m < kMoves; ++m)
      x = std::copy_n(move_.row(id[3 + m]), e, x);
    known.push_back(s);
  }
  const int knownCount = static_cast<int>(known.size());
  pokemon1_.apply(input, knownCount, first, true);
  pokemon2_.apply(first, knownCount, second, true);
  for (int k = 0; k < knownCount; ++k)
    std::copy_n(second + at(k * hidden), hidden, each + at(known[at(k)] * hidden));

  // The trunk: both actives, the mean of each team, the field.
  std::fill_n(trunkIn, b * trunk1_.in, 0.0f);
  for (int q = 0; q < b; ++q) {
    float *row = trunkIn + at(q * trunk1_.in);
    for (int p = 0; p < 2; ++p) {
      float *active = row + at(p * hidden);
      float *mean = row + at((2 + p) * hidden);
      float present = 0.0f;
      for (int i = 0; i < kTeam; ++i) {
        const int s = q * Encoding::kSlots + p * kTeam + i;
        const float *numbers = pokemon + at(s * width);
        const float *h = each + at(s * hidden);
        for (int k = 0; k < hidden; ++k) {
          active[k] += h[k] * numbers[active_];
          mean[k] += h[k] * numbers[present_];
        }
        present += numbers[present_];
      }
      for (int k = 0; k < hidden; ++k)
        mean[k] /= std::max(present, 1.0f);
    }
    std::copy_n(field + at(q * fields), fields, row + 4 * hidden);
  }
  trunk1_.apply(trunkIn, b, t1, true);
  trunk2_.apply(t1, b, trunk, true);

  // A head's first layer reads [trunk, option]: the trunk's share is the same for every option,
  // computed once per position.
  auto head = [&](const Linear &layer1, const Linear &layer2, const float *fromTrunk,
                  const float *fromOption, int perPosition, float *hiddenRows, float *logits) {
    for (int q = 0; q < b; ++q)
      for (int c = 0; c < perPosition; ++c) {
        const float *trunkShare = fromTrunk + at(q * layer1.out);
        const float *optionShare = fromOption + at((q * perPosition + c) * layer1.out);
        float *h = hiddenRows + at((q * perPosition + c) * layer1.out);
        for (int k = 0; k < layer1.out; ++k)
          h[k] = std::max(layer1.bias[at(k)] + trunkShare[k] + optionShare[k], 0.0f);
      }
    layer2.apply(hiddenRows, b * perPosition, logits, false);
  };

  // Each move of the active Pokemon, read with the trunk.
  for (int q = 0; q < b; ++q) {
    const float *team = pokemon + at(q * Encoding::kSlots * width);
    int lead = 0;
    for (int i = 1; i < kTeam; ++i)
      if (team[at(i * width + active_)] > team[at(lead * width + active_)])
        lead = i;
    const int32_t *id = ids.data() + at((q * Encoding::kSlots + lead) * Encoding::kIds);
    for (int m = 0; m < kMoves; ++m) {
      float *x = std::copy_n(move_.row(id[3 + m]), e, options + at((q * kMoves + m) * option));
      std::copy_n(team + at(lead * width + moveColumns_ + m * moveWidth_), moveWidth_, x);
    }
  }
  moves1_.partial(trunk, b, t, 0, t, movesTrunk);
  moves1_.partial(options, b * kMoves, option, t, option, movesOption);
  head(moves1_, moves2_, movesTrunk, movesOption, kMoves, movesHead, moveLogits);

  // Each switch, read with the Pokemon it would send in.
  switch1_.partial(trunk, b, t, 0, t, switchTrunk);
  for (int q = 0; q < b; ++q)
    switch1_.partial(each + at(q * Encoding::kSlots * hidden), kTeam, hidden, t, hidden,
                     switchOption + at(q * kTeam * switch1_.out));
  head(switch1_, switch2_, switchTrunk, switchOption, kTeam, switchHead, switchLogits);

  value_.apply(trunk, b, values, false);
  for (int q = 0; q < b; ++q) {
    Output &o = out[at(q)];
    std::copy_n(moveLogits + at(q * kMoves), kMoves, o.logits.begin());
    std::copy_n(switchLogits + at(q * kTeam), kTeam, o.logits.begin() + kMoves);
    for (int a = 0; a < Encoding::kActions; ++a)
      if (queries[at(q)].mask[a] == 0.0f)
        o.logits[at(a)] = kMasked;
    o.value = std::tanh(values[q]);
  }
}
} // namespace search
