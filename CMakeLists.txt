cmake_minimum_required(VERSION 3.20)
project(battle_engine
    VERSION 0.2.0
    DESCRIPTION "Pokemon battle engine for ft_transcendence"
    LANGUAGES CXX
)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-Wall -Wextra -Wpedantic -Wshadow -Wconversion)
endif()

include(FetchContent)

FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.11.3
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(nlohmann_json)

FetchContent_Declare(
    Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG v3.5.4
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(Catch2)

add_library(battle_engine_lib STATIC
    # model: le vocabulaire du jeu (structs, tables, enums)
    src/model/types.cpp
    src/model/stats.cpp
    src/model/status.cpp
    src/model/move.cpp
    src/model/pokemon.cpp
    src/model/field.cpp
    # core: orchestration et etat
    src/core/events.cpp
    src/core/battle_state.cpp
    src/core/validate.cpp
    src/core/rng.cpp
    src/core/data_loader.cpp
    src/core/switching.cpp
    src/core/engine.cpp
    src/core/struggle.cpp
    # familles extensibles
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
)

target_include_directories(battle_engine_lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

target_link_libraries(battle_engine_lib PUBLIC nlohmann_json::nlohmann_json)

target_compile_definitions(battle_engine_lib PUBLIC
    BATTLE_ENGINE_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data"
)

add_executable(battle_engine_demo demo/main.cpp)
target_link_libraries(battle_engine_demo PRIVATE battle_engine_lib)

enable_testing()
add_subdirectory(tests)
