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
    src/types.cpp
    src/stats.cpp
    src/status.cpp
    src/move.cpp
    src/pokemon.cpp
    src/events.cpp
    src/battle_state.cpp
    src/validate.cpp
    src/rng.cpp
    src/data_loader.cpp
    src/engine.cpp
    src/effects/effect.cpp
    src/effects/damage.cpp
    src/effects/apply_status.cpp
)

target_include_directories(battle_engine_lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

target_link_libraries(battle_engine_lib PUBLIC nlohmann_json::nlohmann_json)

target_compile_definitions(battle_engine_lib PUBLIC
    BATTLE_ENGINE_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data"
)

add_executable(battle_engine_demo src/main.cpp)
target_link_libraries(battle_engine_demo PRIVATE battle_engine_lib)

enable_testing()
add_subdirectory(tests)
