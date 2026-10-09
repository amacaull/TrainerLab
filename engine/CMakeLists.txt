cmake_minimum_required(VERSION 3.20)
project(battle_engine
    VERSION 0.2.0
    DESCRIPTION "Stateless Pokemon battle engine"
    LANGUAGES CXX
)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# The Python build turns both OFF: it needs the library alone, with no test framework fetched.
option(BATTLE_ENGINE_BUILD_TESTS "Build the Catch2 test suite" ON)
option(BATTLE_ENGINE_BUILD_DEMO "Build the demo executable" ON)

# Scoped to our own targets below: add_compile_options() is directory-wide and
# also lands on the FetchContent dependencies, whose warnings are not ours to
# fix and drown ours in the noise.
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    set(BATTLE_ENGINE_WARNINGS -Wall -Wextra -Wpedantic -Wshadow -Wconversion)
endif()

include(FetchContent)

# Prefer a system copy: a network hiccup should not break an offline build.
find_package(nlohmann_json 3.11 QUIET)
if(NOT nlohmann_json_FOUND)
    FetchContent_Declare(
        nlohmann_json
        GIT_REPOSITORY https://github.com/nlohmann/json.git
        GIT_TAG v3.11.3
        GIT_SHALLOW TRUE
    )
    FetchContent_MakeAvailable(nlohmann_json)
endif()

if(BATTLE_ENGINE_BUILD_TESTS)
    FetchContent_Declare(
        Catch2
        GIT_REPOSITORY https://github.com/catchorg/Catch2.git
        GIT_TAG v3.5.4
        GIT_SHALLOW TRUE
    )
    FetchContent_MakeAvailable(Catch2)
endif()

add_library(battle_engine_lib STATIC
    # model: the game's vocabulary (structs, tables, enums)
    src/model/types.cpp
    src/model/stats.cpp
    src/model/status.cpp
    src/model/move.cpp
    src/model/pokemon.cpp
    src/model/field.cpp
    # core: orchestration and state
    src/core/events.cpp
    src/core/battle_state.cpp
    src/core/validate.cpp
    src/core/rng.cpp
    src/core/data_loader.cpp
    src/core/switching.cpp
    src/core/engine.cpp
    src/core/struggle.cpp
    src/core/observation.cpp
    # extensible families
    src/abilities/ability.cpp
    src/abilities/damage_mods.cpp
    src/abilities/immunities.cpp
    src/abilities/weather_abilities.cpp
    src/abilities/switch_hooks.cpp
    src/abilities/triggers.cpp
    src/items/item.cpp
    src/effects/effect.cpp
    src/effects/damage.cpp
    src/effects/apply_status.cpp
    src/effects/stat_change.cpp
    src/effects/pivot.cpp
    src/effects/set_weather.cpp
    src/effects/set_hazard.cpp
    src/effects/set_screen.cpp
    src/effects/clear_hazards.cpp
    src/effects/secondary.cpp
    src/effects/flinch.cpp
    src/effects/move_mechanics.cpp
    src/effects/recoil.cpp
    src/effects/recovery.cpp
    src/effects/force_switch.cpp
    src/effects/protect.cpp
    # boundary: the flat API wrapped by the Python binding
    src/ffi/ffi.cpp
    src/ffi/convert.cpp
)

target_include_directories(battle_engine_lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

target_link_libraries(battle_engine_lib PUBLIC nlohmann_json::nlohmann_json)

# Where the shipped catalog lives. Deliberately NOT a compile definition on
# the library: a silent fallback to a different data/ would shift the frozen
# catalog indices. Library callers pass the path explicitly; only the demo and
# the tests get the convenience macro.
set(BATTLE_ENGINE_DATA_PATH "${CMAKE_CURRENT_SOURCE_DIR}/data")

target_compile_options(battle_engine_lib PRIVATE ${BATTLE_ENGINE_WARNINGS})

# Required to link the archive into the Python extension module.
set_target_properties(battle_engine_lib PROPERTIES POSITION_INDEPENDENT_CODE ON)

if(BATTLE_ENGINE_BUILD_DEMO)
    add_executable(battle_engine_demo demo/main.cpp)
    target_link_libraries(battle_engine_demo PRIVATE battle_engine_lib)
    target_compile_options(battle_engine_demo PRIVATE ${BATTLE_ENGINE_WARNINGS})
    target_compile_definitions(battle_engine_demo PRIVATE
        BATTLE_ENGINE_DATA_DIR="${BATTLE_ENGINE_DATA_PATH}"
    )
endif()

if(BATTLE_ENGINE_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
