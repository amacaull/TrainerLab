// Python binding of the C++ search (search/): what has to be fast, called from Python.
//
// The states are the engine binding's own BattleState objects: trainerlab._engine is imported first
// so that the type is known here.

#include "search/game.hpp"

#include <pybind11/numpy.h>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include <memory>
#include <stdexcept>
#include <string>

namespace py = pybind11;

namespace {
std::unique_ptr<search::Game> game;
std::string dataDir;

const search::Game &loaded() {
  if (!game)
    throw std::runtime_error("trainerlab._search: call init(data_dir) first");
  return *game;
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

  m.def(
      "openings", [](int count, int turns) { return search::openings(loaded(), count, turns); },
      py::arg("count"), py::arg("turns") = 3,
      "Random battles from seeds 0, 1, 2..., played `turns` random turns on, with both actives "
      "standing: the positions of the benchmark.");
}
