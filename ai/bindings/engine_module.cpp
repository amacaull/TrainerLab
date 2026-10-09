// Python binding of the flat engine API (engine/include/engine/ffi/ffi.hpp).
//
// Same function names as the C++ API; struct fields are exposed in snake_case. Enums travel as int,
// with the frozen numbering of engine/README.md, section 6. Errors become Python exceptions, one
// class per prefix, with the full message kept.

#include "engine/ffi/ffi.hpp"

#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <pybind11/typing.h>

#include <array>
#include <cstddef>
#include <cstring>
#include <exception>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace py = pybind11;
using namespace engine;
using namespace engine::ffi;

namespace {
static_assert(std::is_trivially_copyable_v<BattleState>, "pickling copies the bytes");
static_assert(std::is_trivially_copyable_v<BattlePokemon>, "pickling copies the bytes");

// ---------------------------------------------------------------------------------------------
// Errors

// One Python class per prefix, all deriving from EngineError. Created once per process and never
// released: they must outlive any exception in flight at interpreter shutdown.
struct ErrorClass {
  const char *prefix;
  const char *name;
  PyObject *type = nullptr;
};

std::array<ErrorClass, 6> &errorClasses() {
  static std::array<ErrorClass, 6> classes{{
      {"E_INIT", "InitError"},
      {"E_DATA", "DataError"},
      {"E_ARG", "ArgError"},
      {"E_STATE", "StateError"},
      {"E_TEAM", "TeamError"},
      {"E_ACTION", "ActionError"},
  }};
  return classes;
}

bool hasPrefix(const std::string &message, const std::string &prefix) {
  return message.size() > prefix.size() && message.compare(0, prefix.size(), prefix) == 0 &&
         message[prefix.size()] == ':';
}

// "E_ACTION:NO_PP: no PP left..." -> "NO_PP"
std::string actionSubcode(const std::string &message) {
  const size_t start = std::string("E_ACTION:").size();
  const size_t end = message.find(':', start);
  return end == std::string::npos ? std::string() : message.substr(start, end - start);
}

void registerErrors(py::module_ &m) {
  PyObject *base = PyErr_NewExceptionWithDoc(
      "trainerlab._engine.EngineError",
      "A contract violation reported by the engine. str(e) keeps the full message.", nullptr,
      nullptr);
  m.attr("EngineError") = py::handle(base);

  for (ErrorClass &cls : errorClasses()) {
    const std::string qualified = std::string("trainerlab._engine.") + cls.name;
    cls.type = PyErr_NewException(qualified.c_str(), base, nullptr);
    m.attr(cls.name) = py::handle(cls.type);
  }
  // Declared on the class so type checkers see it; each raised ActionError sets its own.
  py::handle(m.attr("ActionError")).attr("subcode") = std::string();

  py::register_exception_translator([](std::exception_ptr p) {
    try {
      if (p)
        std::rethrow_exception(p);
    } catch (const std::exception &e) {
      const std::string message = e.what();
      for (const ErrorClass &cls : errorClasses()) {
        if (!hasPrefix(message, cls.prefix))
          continue;
        py::object error = py::handle(cls.type)(message);
        if (std::string(cls.prefix) == "E_ACTION")
          error.attr("subcode") = actionSubcode(message);
        PyErr_SetObject(cls.type, error.ptr());
        return;
      }
      throw; // no prefix: pybind11's default mapping (ValueError, IndexError...)
    }
  });
}

// ---------------------------------------------------------------------------------------------
// Helpers

void requireSide(int side) {
  if (side != 0 && side != 1)
    throw std::out_of_range("side " + std::to_string(side) + " out of range [0, 2)");
}

void requireSlot(int index) {
  if (index < 0 || index >= kTeamSize)
    throw std::out_of_range("team index " + std::to_string(index) + " out of range [0, 6)");
}

size_t at(int i) { return static_cast<size_t>(i); }

template <typename T, size_t N>
py::typing::Tuple<py::int_, py::ellipsis> toTuple(const std::array<T, N> &values) {
  py::typing::Tuple<py::int_, py::ellipsis> out(N);
  for (size_t i = 0; i < N; ++i)
    out[i] = values[i];
  return out;
}

template <typename T> py::bytes toBytes(const T &value) {
  return py::bytes(reinterpret_cast<const char *>(&value), sizeof(T));
}

template <typename T> T fromBytes(const py::bytes &data) {
  const std::string raw = data;
  if (raw.size() != sizeof(T))
    throw std::invalid_argument(
        "pickled data has the wrong size: built by another engine version?");
  T value;
  std::memcpy(&value, raw.data(), sizeof(T));
  return value;
}

// An array-of-int field: read as a tuple (a list would suggest it can be edited in place), written
// whole.
#define INT_ARRAY_FIELD(cls, name, member)                                                         \
  def_property(                                                                                    \
      name, [](const cls &o) { return toTuple(o.member); },                                        \
      [](cls &o, const decltype(cls::member) &v) { o.member = v; })

#define ENUM_FIELD(cls, name, member, Enum)                                                        \
  def_property(                                                                                    \
      name, [](const cls &o) { return static_cast<int>(o.member); },                               \
      [](cls &o, int v) { o.member = static_cast<Enum>(v); })

// __eq__ taking any object, as Python expects: NotImplemented for a foreign type.
template <typename T, typename Eq> auto equality(Eq eq) {
  return [eq](const T &self, const py::object &other) -> py::object {
    if (!py::isinstance<T>(other))
      return py::reinterpret_borrow<py::object>(Py_NotImplemented);
    return py::bool_(eq(self, other.cast<const T &>()));
  };
}

template <typename T> auto equality() {
  return equality<T>([](const T &a, const T &b) { return a == b; });
}

bool sameAction(const FfiAction &a, const FfiAction &b) {
  return a.kind == b.kind && a.index == b.index && a.pivot_target == b.pivot_target;
}

std::string actionRepr(const FfiAction &a) {
  if (a.kind == 1)
    return "Action.switch(" + std::to_string(a.index) + ")";
  if (a.kind == 0) {
    std::string out = "Action.move(" + std::to_string(a.index);
    if (a.pivot_target != -1)
      out += ", pivot_target=" + std::to_string(a.pivot_target);
    return out + ")";
  }
  return "Action(kind=" + std::to_string(a.kind) + ", index=" + std::to_string(a.index) + ")";
}

FfiAction makeAction(int kind, int index, int pivotTarget) {
  if (kind < 0 || kind > 255)
    throw std::out_of_range("kind " + std::to_string(kind) + " out of range [0, 255]");
  return FfiAction{static_cast<uint8_t>(kind), index, pivotTarget};
}

std::array<BattlePokemon, kTeamSize> toTeamArray(const std::vector<BattlePokemon> &team) {
  if (team.size() > static_cast<size_t>(kTeamSize))
    throw std::invalid_argument("E_TEAM: " + std::to_string(team.size()) + " Pokemon, at most 6");
  std::array<BattlePokemon, kTeamSize> out{};
  for (size_t i = 0; i < team.size(); ++i)
    out[i] = team[i];
  return out;
}

// ---------------------------------------------------------------------------------------------
// Classes

void bindStats(py::module_ &m) {
  py::class_<Stats>(m, "Stats", "Computed battle stats (level 100).")
      .def(py::init<>())
      .def_readwrite("hp", &Stats::hp)
      .def_readwrite("attack", &Stats::atk)
      .def_readwrite("defense", &Stats::def)
      .def_readwrite("special_attack", &Stats::specAtk)
      .def_readwrite("special_defense", &Stats::specDef)
      .def_readwrite("speed", &Stats::speed)
      .def("__eq__", equality<Stats>())
      .def("__repr__", [](const Stats &s) {
        return "Stats(hp=" + std::to_string(s.hp) + ", attack=" + std::to_string(s.atk) +
               ", defense=" + std::to_string(s.def) +
               ", special_attack=" + std::to_string(s.specAtk) +
               ", special_defense=" + std::to_string(s.specDef) +
               ", speed=" + std::to_string(s.speed) + ")";
      });
}

void bindPokemon(py::module_ &m) {
  py::class_<BattlePokemon>(m, "BattlePokemon",
                            "One team slot. Build it with make_combatant, never field by field.")
      .def(py::init<>())
      .def_readwrite("species_id", &BattlePokemon::species_id)
      .def_readwrite("level", &BattlePokemon::level)
      .def_readwrite("stats", &BattlePokemon::stats)
      .def_readwrite("current_hp", &BattlePokemon::currentHp)
      .INT_ARRAY_FIELD(BattlePokemon, "move_ids", move_ids)
      .INT_ARRAY_FIELD(BattlePokemon, "pp", pp)
      .def_readwrite("item_id", &BattlePokemon::item_id)
      .def_readwrite("item_consumed", &BattlePokemon::item_consumed)
      .def_readwrite("locked_move_id", &BattlePokemon::locked_move_id)
      .def_readwrite("disguise_broken", &BattlePokemon::disguise_broken)
      .def_readwrite("flash_fire_active", &BattlePokemon::flash_fire_active)
      .ENUM_FIELD(BattlePokemon, "status", status, Status)
      .def_readwrite("status_turns", &BattlePokemon::status_turns)
      .def_readwrite("sleep_self_inflicted", &BattlePokemon::sleep_self_inflicted)
      .INT_ARRAY_FIELD(BattlePokemon, "stat_stages", stat_stages)
      .def_readwrite("flinched", &BattlePokemon::flinched)
      .def_readwrite("roosted", &BattlePokemon::roosted)
      .def_readwrite("protected_now", &BattlePokemon::protected_now)
      .def_readwrite("protect_chain", &BattlePokemon::protect_chain)
      .def_readwrite("charging_move_id", &BattlePokemon::charging_move_id)
      .def_readwrite("invulnerable_state", &BattlePokemon::invulnerable_state)
      .def_readwrite("turns_on_field", &BattlePokemon::turns_on_field)
      .def_readwrite("last_move_id", &BattlePokemon::last_move_id)
      .def_readwrite("destiny_bond_active", &BattlePokemon::destiny_bond_active)
      .def_readwrite("protect_contact_status", &BattlePokemon::protect_contact_status)
      .def_readwrite("revealed", &BattlePokemon::revealed)
      .def("is_fainted", &BattlePokemon::isFainted)
      .def("is_empty", &BattlePokemon::isEmpty, "True for a slot hidden by observe or unused.")
      .def("__eq__", equality<BattlePokemon>())
      .def("__copy__", [](const BattlePokemon &p) { return p; })
      .def("__deepcopy__", [](const BattlePokemon &p, const py::dict &) { return p; })
      .def(py::pickle([](const BattlePokemon &p) { return toBytes(p); },
                      [](const py::bytes &b) { return fromBytes<BattlePokemon>(b); }))
      .def("__repr__", [](const BattlePokemon &p) {
        return "BattlePokemon(species_id=" + std::to_string(p.species_id) +
               ", current_hp=" + std::to_string(p.currentHp) + "/" + std::to_string(p.stats.hp) +
               ", status=" + std::to_string(static_cast<int>(p.status)) + ")";
      });
}

void bindState(py::module_ &m) {
  py::class_<SideHazards>(m, "SideHazards", "Hazard layers on one side.")
      .def(py::init<>())
      .def_readwrite("stealth_rock", &SideHazards::stealth_rock)
      .def_readwrite("spikes", &SideHazards::spikes)
      .def_readwrite("toxic_spikes", &SideHazards::toxic_spikes)
      .def("__eq__", equality<SideHazards>());

  py::class_<BattleState>(m, "BattleState",
                          "The whole battle, owned by the caller. Accessors return copies: write "
                          "back with the matching set_* method.")
      .def(py::init<>())
      .def(
          "pokemon",
          [](const BattleState &s, int side, int index) {
            requireSide(side);
            requireSlot(index);
            return s.teams[at(side)][at(index)];
          },
          py::arg("side"), py::arg("index"), "A copy of one team slot.")
      .def(
          "set_pokemon",
          [](BattleState &s, int side, int index, const BattlePokemon &p) {
            requireSide(side);
            requireSlot(index);
            s.teams[at(side)][at(index)] = p;
          },
          py::arg("side"), py::arg("index"), py::arg("pokemon"))
      .def(
          "active",
          [](const BattleState &s, int side) {
            requireSide(side);
            return s.active(side);
          },
          py::arg("side"), "A copy of this side's active Pokemon.")
      .def(
          "set_team",
          [](BattleState &s, int side, const std::vector<BattlePokemon> &team) {
            requireSide(side);
            if (team.empty() || team.size() > static_cast<size_t>(kTeamSize))
              throw std::invalid_argument("a team has 1 to 6 Pokemon, got " +
                                          std::to_string(team.size()));
            s.teams[at(side)] = std::array<BattlePokemon, kTeamSize>{};
            for (size_t i = 0; i < team.size(); ++i)
              s.teams[at(side)][i] = team[i];
            s.team_size[at(side)] = static_cast<int>(team.size());
            s.activeIndex[at(side)] = 0;
          },
          py::arg("side"), py::arg("team"),
          "Fills a side from slot 0, sets team_size, and makes slot 0 the lead.")
      .def(
          "hazards",
          [](const BattleState &s, int side) {
            requireSide(side);
            return s.hazards[at(side)];
          },
          py::arg("side"))
      .def(
          "set_hazards",
          [](BattleState &s, int side, const SideHazards &h) {
            requireSide(side);
            s.hazards[at(side)] = h;
          },
          py::arg("side"), py::arg("hazards"))
      .INT_ARRAY_FIELD(BattleState, "team_size", team_size)
      .INT_ARRAY_FIELD(BattleState, "active_index", activeIndex)
      .def_readwrite("turn", &BattleState::turn)
      .ENUM_FIELD(BattleState, "weather", weather, Weather)
      .def_readwrite("weather_turns_left", &BattleState::weather_turns_left)
      .ENUM_FIELD(BattleState, "terrain", terrain, Terrain)
      .def_readwrite("terrain_turns_left", &BattleState::terrain_turns_left)
      .INT_ARRAY_FIELD(BattleState, "aurora_veil_turns", aurora_veil_turns)
      .INT_ARRAY_FIELD(BattleState, "wish_turns", wish_turns)
      .INT_ARRAY_FIELD(BattleState, "wish_heal", wish_heal)
      .def("__eq__", equality<BattleState>())
      .def("__copy__", [](const BattleState &s) { return s; })
      .def("__deepcopy__", [](const BattleState &s, const py::dict &) { return s; })
      // Raw bytes: fine between processes of one build (multiprocessing), not a storage format.
      .def(py::pickle([](const BattleState &s) { return toBytes(s); },
                      [](const py::bytes &b) { return fromBytes<BattleState>(b); }));
}

void bindActionsAndEvents(py::module_ &m) {
  py::class_<FfiAction>(m, "Action", "A turn decision. Build with Action.move or Action.switch.")
      .def(py::init(&makeAction), py::arg("kind"), py::arg("index"), py::arg("pivot_target") = -1)
      .def_static(
          "move", [](int slot, int pivotTarget) { return FfiAction{0, slot, pivotTarget}; },
          py::arg("slot"), py::arg("pivot_target") = -1)
      .def_static(
          "switch", [](int teamIndex) { return FfiAction{1, teamIndex, -1}; },
          py::arg("team_index"))
      .def_property_readonly("kind", [](const FfiAction &a) { return static_cast<int>(a.kind); })
      .def_property_readonly("index", [](const FfiAction &a) { return a.index; })
      .def_property_readonly("pivot_target", [](const FfiAction &a) { return a.pivot_target; })
      .def("__eq__", equality<FfiAction>(&sameAction))
      .def("__hash__",
           [](const FfiAction &a) {
             return py::hash(py::make_tuple(static_cast<int>(a.kind), a.index, a.pivot_target));
           })
      .def("__repr__", &actionRepr);

  py::class_<FfiEvent>(m, "Event",
                       "One engine event. The meaning of i0, i1, f0 and flags depends on kind: "
                       "see the event table in engine/README.md, section 6.")
      .def_property_readonly("kind", [](const FfiEvent &e) { return static_cast<int>(e.kind); })
      .def_property_readonly("side", [](const FfiEvent &e) { return static_cast<int>(e.side); })
      .def_property_readonly("slot", [](const FfiEvent &e) { return static_cast<int>(e.slot); })
      .def_readonly("name_id", &FfiEvent::name_id)
      .def_readonly("i0", &FfiEvent::i0)
      .def_readonly("i1", &FfiEvent::i1)
      .def_readonly("f0", &FfiEvent::f0)
      .def_property_readonly("flags", [](const FfiEvent &e) { return static_cast<int>(e.flags); })
      .def("__repr__", [](const FfiEvent &e) {
        return "Event(kind=" + std::to_string(e.kind) + ", side=" + std::to_string(e.side) +
               ", slot=" + std::to_string(e.slot) + ", name_id=" + std::to_string(e.name_id) +
               ", i0=" + std::to_string(e.i0) + ", i1=" + std::to_string(e.i1) + ")";
      });

  py::class_<SpeciesEntry>(m, "SpeciesEntry")
      .def_readonly("id_string", &SpeciesEntry::id_string)
      .def_readonly("display_name", &SpeciesEntry::display_name)
      .def_readonly("type1", &SpeciesEntry::type1)
      .def_readonly("type2", &SpeciesEntry::type2)
      .def_readonly("weight_kg", &SpeciesEntry::weight_kg)
      .def_readonly("mega", &SpeciesEntry::mega)
      .def_readonly("legendary", &SpeciesEntry::legendary);

  py::class_<MoveEntry>(m, "MoveEntry")
      .def_readonly("name", &MoveEntry::name)
      .def_readonly("type", &MoveEntry::type)
      .def_readonly("category", &MoveEntry::category)
      .def_readonly("power", &MoveEntry::power)
      .def_readonly("accuracy", &MoveEntry::accuracy)
      .def_readonly("pp", &MoveEntry::pp)
      .def_readonly("priority", &MoveEntry::priority);
}

// ---------------------------------------------------------------------------------------------
// Functions

void bindFunctions(py::module_ &m) {
  m.def("engine_init", &engine_init, py::arg("data_dir"),
        "Loads the catalog. Idempotent for the same path; another path raises InitError.");
  m.def("engine_is_initialised", &engine_is_initialised);
  m.def("engine_data_dir", &engine_data_dir);

  m.def("species_count", &species_count);
  m.def("move_count", &move_count);
  m.def("item_count", &item_count);
  m.def("ability_count", &ability_count);

  m.def("find_species_id", &find_species_id, py::arg("id_string"), "-1 when unknown.");
  m.def("find_move_id", &find_move_id, py::arg("name"), "-1 when unknown.");
  m.def("find_item_id", &find_item_id, py::arg("name"), "-1 when unknown.");
  m.def("find_ability_id", &find_ability_id, py::arg("name"), "-1 when unknown.");

  m.def("species_id_string", &species_id_string, py::arg("id"));
  m.def("species_display_name", &species_display_name, py::arg("id"));
  m.def("move_name", &move_name, py::arg("id"));
  m.def("item_name", &item_name, py::arg("id"));
  m.def("ability_name", &ability_name, py::arg("id"));
  m.def("species_entry", &species_entry, py::arg("id"));
  m.def("move_entry", &move_entry, py::arg("id"));
  m.def("struggle_move_id", &struggle_move_id);
  m.def("struggle_recoil_ability_id", &struggle_recoil_ability_id);
  m.def("catalog_fingerprint", &catalog_fingerprint);

  m.def("make_combatant", &make_combatant, py::arg("species_id"));
  m.def(
      "validate_team",
      [](const std::vector<BattlePokemon> &team) {
        validate_team(toTeamArray(team), static_cast<int>(team.size()));
      },
      py::arg("team"), "Raises TeamError when the team breaks a team building rule.");
  m.def("validate_state", &validate_state, py::arg("state"));

  m.def("start_battle", &start_battle, py::arg("state"), py::arg("seed"),
        "Mandatory before the first turn. Updates state in place, returns the events.");
  m.def("resolve_turn", &resolve_turn, py::arg("state"), py::arg("action0"), py::arg("action1"),
        py::arg("seed"),
        "Updates state in place, returns the events. On error, state is left untouched.");
  m.def("resolve_replacement", &resolve_replacement, py::arg("state"), py::arg("side"),
        py::arg("team_index"));
  m.def("faster_side", &faster_side, py::arg("state"), py::arg("seed"));
  m.def("is_over", &is_over, py::arg("state"));
  m.def("side_has_lost", &side_has_lost, py::arg("state"), py::arg("side"));

  m.def("legal_actions", &legal_actions, py::arg("state"), py::arg("side"),
        "What resolve_turn accepts for this side, one entry per distinct outcome. Empty when the "
        "active is fainted.");
  m.def("legal_replacements", &legal_replacements, py::arg("state"), py::arg("side"),
        "Team indices resolve_replacement accepts. Empty unless the active is fainted.");
  m.def("observe", &observe, py::arg("state"), py::arg("side"),
        "What the player on this side can see. A view: never pass it back to the engine, "
        "legal_actions included. Compute legal actions from the real state.");
}

void bindConstants(py::module_ &m) {
  m.attr("TEAM_SIZE") = kTeamSize;
  m.attr("MAX_MOVES") = kMaxMovesPerPokemon;
  m.attr("NO_MOVE") = kNoMove;
  m.attr("NO_SPECIES") = kNoSpecies;
  m.attr("NO_ITEM") = kNoItem;
  m.attr("NO_NAME") = kFfiNoName;
  m.attr("STRUGGLE") = kFfiStruggle;
  m.attr("STRUGGLE_RECOIL") = kFfiStruggleRecoil;
  m.attr("FLAG_STAB") = static_cast<int>(kFfiFlagStab);
  m.attr("FLAG_CRIT") = static_cast<int>(kFfiFlagCrit);
  m.attr("ACTION_MOVE") = 0;
  m.attr("ACTION_SWITCH") = 1;
}
} // namespace

PYBIND11_MODULE(_engine, m) {
  m.doc() = "TrainerLab battle engine: the flat C++ API, unchanged.";
  registerErrors(m);
  bindStats(m);
  bindPokemon(m);
  bindState(m);
  bindActionsAndEvents(m);
  bindFunctions(m);
  bindConstants(m);
}
