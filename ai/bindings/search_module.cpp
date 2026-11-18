// Python binding of the C++ search (search/): what has to be fast, called from Python.
//
// The states are the engine binding's own BattleState objects: trainerlab._engine is imported first
// so that the type is known here.

#include "search/forecaster.hpp"
#include "search/game.hpp"
#include "search/mcts.hpp"
#include "search/vec_env.hpp"

#include "engine/ffi/convert.hpp"
#include "engine/ffi/ffi.hpp"

#include <pybind11/numpy.h>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include <memory>
#include <stdexcept>
#include <string>
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

  m.def(
      "openings", [](int count, int turns) { return search::openings(loaded(), count, turns); },
      py::arg("count"), py::arg("turns") = 3,
      "Random battles from seeds 0, 1, 2..., played `turns` random turns on, with both actives "
      "standing: the positions of the benchmark.");
}
