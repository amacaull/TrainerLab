// Python binding of the C++ search (search/): what has to be fast, called from Python.
//
// The states are the engine binding's own BattleState objects: trainerlab._engine is imported first
// so that the type is known here.

#include "search/forecaster.hpp"
#include "search/game.hpp"

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

  py::class_<search::Forecaster>(
      m, "Forecaster",
      "Rules 6 and 7 in C++, with the layers of a WinModel: the same choices as "
      "trainerlab.bots.lookahead.")
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
      "openings", [](int count, int turns) { return search::openings(loaded(), count, turns); },
      py::arg("count"), py::arg("turns") = 3,
      "Random battles from seeds 0, 1, 2..., played `turns` random turns on, with both actives "
      "standing: the positions of the benchmark.");
}
