#pragma once

#include <string>

// Boundary layer for the Rust backend (cxx, ADR D1) and the Python AI service
// (pybind11, ADR #52). Plain C++ on purpose: the bridge and its rust::Str
// adapter live on the caller's side, so this header serves both.
//
// Every function may throw. Messages carry a stable prefix - E_INIT, E_DATA,
// E_ARG here, E_STATE / E_TEAM / E_ACTION from 16b-1 - because cxx transports
// what() only. Full contract in FFI-CONTRACT.md.
namespace engine::ffi {

// Idempotent for the same path. A different path throws: two catalogs in one
// process would mean two id spaces (ADR #57).
void engine_init(const std::string &dataDir);

bool engine_is_initialised();
std::string engine_data_dir();

int species_count();
int move_count();
int item_count();
int ability_count();

// A miss is -1, not an error; only an uninitialised engine throws. Species
// take the id_string - the JSON filename, English canon (ADR #48) - not the
// display name.
int find_species_id(const std::string &id_string);
int find_move_id(const std::string &name);
int find_item_id(const std::string &name);
int find_ability_id(const std::string &name);

// Throw E_ARG on an invalid id: unlike a lookup miss, that is a caller bug.
std::string species_id_string(int id);
std::string species_display_name(int id);
std::string move_name(int id);
std::string item_name(int id);
std::string ability_name(int id);

} // namespace engine::ffi
