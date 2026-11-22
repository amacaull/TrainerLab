// Python binding of the C++ search (search/): what has to be fast, called from Python.
//
// The states are the engine binding's own BattleState objects: trainerlab._engine is imported first
// so that the type is known here.

#include "search/forecaster.hpp"
#include "search/game.hpp"
#include "search/mcts.hpp"
#include "search/network.hpp"
#include "search/planner.hpp"
#include "search/self_play.hpp"
#include "search/vec_env.hpp"

#include "engine/ffi/convert.hpp"
#include "engine/ffi/ffi.hpp"

#include <pybind11/numpy.h>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include <algorithm>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

namespace py = pybind11;

namespace {
std::unique_ptr<search::Game> game;
std::string dataDir;

const search::Game &loaded() {
  if (!game)
    throw std::runtime_error("trainerlab._search: call init(data_dir) first");
  return *game;
}

std::vector<engine::Action> toActions(const std::vector<engine::ffi::FfiAction> &legal) {
  std::vector<engine::Action> actions;
  actions.reserve(legal.size());
  for (const engine::ffi::FfiAction &action : legal)
    actions.push_back(engine::ffi::toAction(action));
  return actions;
}

std::vector<double> flat(const py::array_t<double, py::array::c_style | py::array::forcecast> &a) {
  return {a.data(), a.data() + a.size()};
}
} // namespace

PYBIND11_MODULE(_search, m) {
  m.doc() = "TrainerLab search: the C++ side of the AI.";
  py::module_::import("trainerlab._engine");

  m.def(
      "init",
      [](const std::string &dir) {
        if (game && dir != dataDir)
          throw std::runtime_error("trainerlab._search: already loaded from " + dataDir);
        if (!game) {
          game = std::make_unique<search::Game>(dir);
          dataDir = dir;
        }
      },
      py::arg("data_dir"), "Loads the catalog once; again with the same path does nothing.");

  m.def(
      "feature_names", [] { return loaded().features().names(); },
      "The name of each column of features(), in order.");

  m.def(
      "features",
      [](const engine::BattleState &view, int side) {
        const search::Features &features = loaded().features();
        py::array_t<float> row(static_cast<py::ssize_t>(features.names().size()));
        features.compute(view, side, row.mutable_data());
        return row;
      },
      py::arg("view"), py::arg("side"),
      "What `side` saw in `view`, its own observe(), as float32: the same values as "
      "trainerlab.learning.features.features.");

  py::class_<search::Forecaster>(m, "Forecaster",
                                 "Rules 6 to 9 in C++, with the layers of a WinModel.")
      .def(py::init([](const std::vector<py::array_t<double>> &weights,
                       const std::vector<py::array_t<double>> &biases,
                       const py::array_t<double> &mean, const py::array_t<double> &std) {
             search::WinModel model;
             for (const auto &w : weights)
               model.weights.push_back(flat(w));
             for (const auto &b : biases)
               model.biases.push_back(flat(b));
             model.mean = flat(mean);
             model.std = flat(std);
             return search::Forecaster(loaded(), std::move(model));
           }),
           py::arg("weights"), py::arg("biases"), py::arg("mean"), py::arg("std"))
      .def(
          "choose_action",
          [](const search::Forecaster &f, const engine::BattleState &view, int side,
             const std::vector<engine::ffi::FfiAction> &legal) {
            return f.chooseAction(view, side, toActions(legal));
          },
          py::arg("view"), py::arg("side"), py::arg("legal"),
          "The index, in `legal`, of the forecaster's choice (rule 6).")
      .def(
          "choose_strategist_action",
          [](const search::Forecaster &f, const engine::BattleState &view, int side,
             const std::vector<engine::ffi::FfiAction> &legal) {
            return f.chooseStrategistAction(view, side, toActions(legal));
          },
          py::arg("view"), py::arg("side"), py::arg("legal"),
          "The index, in `legal`, of the strategist's choice (rule 7).")
      .def(
          "nash_strategy",
          [](const search::Forecaster &f, const engine::BattleState &view, int side,
             const std::vector<engine::ffi::FfiAction> &legal) {
            return f.nashStrategy(view, side, toActions(legal));
          },
          py::arg("view"), py::arg("side"), py::arg("legal"),
          "Rule 8: one probability per option in `legal`, the optimal mixed strategy of the "
          "simultaneous game the odds define.")
      .def(
          "mcts_action",
          [](const search::Forecaster &f, const engine::BattleState &view, int side,
             const std::vector<engine::ffi::FfiAction> &legal, uint64_t seed) {
            return search::Mcts(loaded(), f).chooseAction(view, side, toActions(legal), seed);
          },
          py::arg("view"), py::arg("side"), py::arg("legal"), py::arg("seed"),
          "Rule 9: the index, in `legal`, of the option a Monte Carlo tree search over "
          "simultaneous moves visits most; the same seed makes the same decision.")
      .def("choose_replacement", &search::Forecaster::chooseReplacement, py::arg("view"),
           py::arg("side"), py::arg("options"), "The index, in `options`, of the Pokemon sent in.")
      .def("odds", &search::Forecaster::odds, py::arg("state"), py::arg("side"),
           "The probability of winning from this position, as the model sees it.")
      .def(
          "calculator_action",
          [](const search::Forecaster &f, const engine::BattleState &view, int side,
             const std::vector<engine::ffi::FfiAction> &legal) {
            return engine::ffi::toFfiAction(f.calculatorAction(view, side, toActions(legal)));
          },
          py::arg("view"), py::arg("side"), py::arg("legal"),
          "What rule 5 plays from this view: the foe the forecaster assumes.");

  m.def(
      "encoding_names",
      [] {
        const search::Encoding &e = loaded().encoding();
        return py::make_tuple(e.pokemonNames(), e.fieldNames());
      },
      "The names of the columns of a Pokemon's vector, and of the field's.");

  m.def(
      "encode",
      [](const engine::BattleState &view, int side) {
        const search::Encoding &e = loaded().encoding();
        const auto slots = static_cast<py::ssize_t>(search::Encoding::kSlots);
        py::array_t<float> pokemon({slots, static_cast<py::ssize_t>(e.pokemonNames().size())});
        py::array_t<int32_t> ids({slots, static_cast<py::ssize_t>(search::Encoding::kIds)});
        py::array_t<float> field(static_cast<py::ssize_t>(e.fieldNames().size()));
        e.encode(view, side, pokemon.mutable_data(), ids.mutable_data(), field.mutable_data());
        return py::make_tuple(pokemon, ids, field);
      },
      py::arg("view"), py::arg("side"),
      "What `side` sees, for a network: (pokemon[12, P], ids[12, 7], field[F]).");

  m.def(
      "action_mask",
      [](const std::vector<engine::ffi::FfiAction> &legal) {
        py::array_t<float> out(static_cast<py::ssize_t>(search::Encoding::kActions));
        search::Encoding::mask(toActions(legal), out.mutable_data());
        return out;
      },
      py::arg("legal"), "1 for each legal option: moves 0-3 by slot, then switches 4-9.");

  py::class_<search::VecEnv>(
      m, "VecEnv",
      "Many battles at once, both sides played by the caller; a battle that ends starts again.")
      .def(py::init([](int count, uint64_t seed, int max_turns) {
             return search::VecEnv(loaded(), count, seed, max_turns);
           }),
           py::arg("count"), py::arg("seed"), py::arg("max_turns") = 1000)
      .def_property_readonly("count", &search::VecEnv::count)
      .def_property_readonly("finished", &search::VecEnv::finished,
                             "Battles finished since the start.")
      .def(
          "observe",
          [](const search::VecEnv &env) {
            const search::Encoding &e = loaded().encoding();
            const auto n = static_cast<py::ssize_t>(env.count());
            const auto slots = static_cast<py::ssize_t>(search::Encoding::kSlots);
            py::array_t<float> pokemon(
                {n, py::ssize_t{2}, slots, static_cast<py::ssize_t>(e.pokemonNames().size())});
            py::array_t<int32_t> ids(
                {n, py::ssize_t{2}, slots, static_cast<py::ssize_t>(search::Encoding::kIds)});
            py::array_t<float> field(
                {n, py::ssize_t{2}, static_cast<py::ssize_t>(e.fieldNames().size())});
            py::array_t<float> mask(
                {n, py::ssize_t{2}, static_cast<py::ssize_t>(search::Encoding::kActions)});
            env.observe(pokemon.mutable_data(), ids.mutable_data(), field.mutable_data(),
                        mask.mutable_data());
            return py::make_tuple(pokemon, ids, field, mask);
          },
          "(pokemon[N, 2, 12, P], ids[N, 2, 12, 7], field[N, 2, F], mask[N, 2, 10]), per "
          "battle and side.")
      .def(
          "step",
          [](search::VecEnv &env,
             const py::array_t<int32_t, py::array::c_style | py::array::forcecast> &actions) {
            const auto n = static_cast<py::ssize_t>(env.count());
            if (actions.size() != 2 * n)
              throw std::invalid_argument("VecEnv.step: one action per battle and side");
            py::array_t<float> rewards({n, py::ssize_t{2}});
            py::array_t<uint8_t> done(n);
            env.step(actions.data(), rewards.mutable_data(), done.mutable_data());
            return py::make_tuple(rewards, done.attr("astype")("bool"));
          },
          py::arg("actions"),
          "Plays actions[N, 2]; (rewards[N, 2], done[N]). A side with no decision is ignored.");

  py::class_<search::Network>(
      m, "Network", "The policy and value network of trainerlab.rl.net, evaluated in C++.")
      .def(py::init([](const std::map<std::string,
                                      py::array_t<float, py::array::c_style | py::array::forcecast>>
                           &weights) {
             std::map<std::string, search::Network::Tensor> tensors;
             for (const auto &[name, array] : weights) {
               search::Network::Tensor t;
               for (py::ssize_t d = 0; d < array.ndim(); ++d)
                 t.shape.push_back(static_cast<int>(array.shape(d)));
               t.values.assign(array.data(), array.data() + array.size());
               tensors.emplace(name, std::move(t));
             }
             return search::Network(loaded().encoding(), tensors);
           }),
           py::arg("weights"), "The weights by their PyTorch names (state_dict).")
      .def(
          "evaluate",
          [](const search::Network &net, const engine::BattleState &view, int side,
             const py::array_t<float, py::array::c_style | py::array::forcecast> &mask) {
            if (mask.size() != search::Encoding::kActions)
              throw std::invalid_argument("Network.evaluate: a mask of 10 options");
            const search::Network::Output out = net.evaluate(view, side, mask.data());
            py::array_t<float> logits(search::Encoding::kActions);
            std::copy(out.logits.begin(), out.logits.end(), logits.mutable_data());
            return py::make_tuple(logits, out.value);
          },
          py::arg("view"), py::arg("side"), py::arg("mask"),
          "(logits[10], value) for what `side` sees in `view`, its own observe().")
      .def(
          "plan_action",
          [](const search::Network &net, const engine::BattleState &view, int side,
             const std::vector<engine::ffi::FfiAction> &legal, uint64_t seed, int simulations) {
            if (simulations < 1)
              throw std::invalid_argument("plan_action: at least one simulation");
            search::PlannerSettings settings;
            settings.simulations = simulations;
            return search::Planner(loaded(), net, settings)
                .chooseAction(view, side, toActions(legal), seed);
          },
          py::arg("view"), py::arg("side"), py::arg("legal"), py::arg("seed"),
          py::arg("simulations") = 200,
          "Rule 11: the index, in `legal`, of the option a tree search guided by this network "
          "visits most, after `simulations` simulations; the same seed makes the same decision.")
      .def(
          "plan_replacement",
          [](const search::Network &net, const engine::BattleState &view, int side,
             const std::vector<int> &options, uint64_t seed, int simulations) {
            if (options.empty())
              throw std::invalid_argument("plan_replacement: at least one option");
            const py::gil_scoped_release free;
            return search::Planner(loaded(), net)
                .searchReplacement(view, side, options, seed, simulations);
          },
          py::arg("view"), py::arg("side"), py::arg("options"), py::arg("seed"),
          py::arg("simulations"),
          "Palier 10.4: the team index, among `options`, whose replacement leaves `side` the best "
          "value, each searched `simulations` simulations; the same seed makes the same choice.")
      .def(
          "judge",
          [](const search::Network &net, const engine::BattleState &view, int side,
             const std::vector<engine::ffi::FfiAction> &legal, uint64_t seed, int simulations,
             int worlds, int min_visits) {
            search::PlannerSettings settings;
            settings.simulations = simulations;
            settings.worlds = worlds;
            settings.minRootVisits = min_visits;
            search::Planner::Analysis analysis;
            {
              const py::gil_scoped_release free;
              analysis = search::Planner(loaded(), net, settings)
                             .analyse(view, side, toActions(legal), seed);
            }
            py::array_t<int32_t> visits(search::Encoding::kActions);
            py::array_t<double> value(search::Encoding::kActions);
            std::copy(analysis.visits.begin(), analysis.visits.end(), visits.mutable_data());
            std::copy(analysis.value.begin(), analysis.value.end(), value.mutable_data());
            return py::make_tuple(visits, value);
          },
          py::arg("view"), py::arg("side"), py::arg("legal"), py::arg("seed"),
          py::arg("simulations"), py::arg("worlds"), py::arg("min_visits"),
          "A judge's look at a decision: (visits[10], value[10]) per option of `side` at the root "
          "of a search that tries every legal option at least `min_visits` times; the value is "
          "the mean result for `side`, in [-1, 1].")
      .def(
          "self_play",
          [](const search::Network &net, int games, uint64_t seed, int threads, int simulations,
             int worlds, double noise, int max_turns, int lanes, int fast, double full_share,
             int replacement_simulations, const std::vector<std::pair<int, double>> &team_sizes) {
            search::SelfPlaySettings settings;
            settings.planner.simulations = simulations;
            settings.planner.worlds = worlds;
            settings.planner.noise = noise;
            settings.maxTurns = max_turns;
            settings.lanes = lanes;
            settings.fast = fast;
            settings.fullShare = full_share;
            settings.replacementSimulations = replacement_simulations;
            settings.teamSizes = team_sizes;
            search::SelfPlayData data;
            {
              const py::gil_scoped_release free; // the threads run without Python
              data = search::selfPlay(loaded(), net, settings, games, seed, threads);
            }
            const search::Encoding &e = loaded().encoding();
            const auto rows = static_cast<py::ssize_t>(data.rows());
            const auto p = static_cast<py::ssize_t>(e.pokemonNames().size());
            const auto f = static_cast<py::ssize_t>(e.fieldNames().size());
            constexpr py::ssize_t slots = search::Encoding::kSlots, ids = search::Encoding::kIds;
            constexpr py::ssize_t actions = search::Encoding::kActions;
            auto array = [](const auto &values, std::vector<py::ssize_t> shape) {
              using T = typename std::decay_t<decltype(values)>::value_type;
              py::array_t<T> out(shape);
              std::copy(values.begin(), values.end(), out.mutable_data());
              return out;
            };
            py::dict out;
            out["pokemon"] = array(data.pokemon, {rows, slots, p});
            out["ids"] = array(data.ids, {rows, slots, ids});
            out["field"] = array(data.field, {rows, f});
            out["mask"] = array(data.mask, {rows, actions});
            out["policy"] = array(data.policy, {rows, actions});
            out["result"] = array(data.result, {rows});
            out["search_value"] = array(data.searchValue, {rows});
            out["foe"] = array(data.foe, {rows});
            out["foe_mask"] = array(data.foeMask, {rows, actions});
            out["game"] = array(data.game, {rows});
            out["turns"] = array(data.turns, {static_cast<py::ssize_t>(data.turns.size())});
            out["winner"] = array(data.winner, {static_cast<py::ssize_t>(data.winner.size())});
            out["team_size"] =
                array(data.teamSize, {static_cast<py::ssize_t>(data.teamSize.size())});
            return out;
          },
          py::arg("games"), py::arg("seed"), py::arg("threads"), py::arg("simulations") = 200,
          py::arg("worlds") = 8, py::arg("noise") = 0.25, py::arg("max_turns") = 1000,
          py::arg("lanes") = 16, py::arg("fast") = 0, py::arg("full_share") = 1.0,
          py::arg("replacement_simulations") = 0,
          py::arg("team_sizes") = std::vector<std::pair<int, double>>{{engine::kTeamSize, 1.0}},
          "The AlphaZero loop's games (search/include/search/self_play.hpp): rule 11 against "
          "itself, with noise at the root; per decision the encoding, the mask, the share of "
          "visits per option (policy), the result for that side, the search's value for it "
          "(search_value) and what the foe played (foe, "
          "foe_mask); per battle its turns and "
          "winner (-1 for a draw). Each thread plays `lanes` battles side by side, their network "
          "queries batched. With `fast`, only a share `full_share` of the searches are full and "
          "recorded; the others search `fast` simulations, unrecorded. With "
          "`replacement_simulations`, each replacement with a choice is searched and recorded "
          "(foe -1). Teams are cut to `team_sizes` (size, share) pairs, drawn per battle "
          "(team_size).");

  m.def(
      "openings", [](int count, int turns) { return search::openings(loaded(), count, turns); },
      py::arg("count"), py::arg("turns") = 3,
      "Random battles from seeds 0, 1, 2..., played `turns` random turns on, with both actives "
      "standing: the positions of the benchmark.");
}
