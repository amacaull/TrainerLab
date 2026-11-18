cmake_minimum_required(VERSION 3.20)
project(trainerlab_search
    DESCRIPTION "Search over the battle engine: playouts, then Nash and MCTS"
    LANGUAGES CXX
)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# A benchmark in Debug measures nothing: Release unless asked otherwise.
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release)
endif()

option(SEARCH_BUILD_TESTS "Build the Catch2 test suite and the benchmark" ON)

# The engine as a library: its own tests and demo stay in engine/.
set(BATTLE_ENGINE_BUILD_TESTS OFF)
set(BATTLE_ENGINE_BUILD_DEMO OFF)
# Unless a parent (the Python build) already added it.
if(NOT TARGET battle_engine_lib)
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../engine ${CMAKE_CURRENT_BINARY_DIR}/engine)
endif()

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    set(SEARCH_WARNINGS -Wall -Wextra -Wpedantic -Wshadow -Wconversion)
endif()

add_library(search_lib STATIC
    src/game.cpp
    src/features.cpp
    src/estimate.cpp
    src/forecaster.cpp
    src/matrix_game.cpp
    src/mcts.cpp
)
target_include_directories(search_lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(search_lib PUBLIC battle_engine_lib)
target_compile_options(search_lib PRIVATE ${SEARCH_WARNINGS})
# Same reason as the engine: a search that sums probabilities must rank alike on macOS and Linux.
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(search_lib PRIVATE -ffp-contract=off)
endif()
set_target_properties(search_lib PROPERTIES POSITION_INDEPENDENT_CODE ON)

if(SEARCH_BUILD_TESTS)
    set(TRAINERLAB_DATA_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../engine/data")

    add_executable(search_bench bench/bench.cpp)
    target_link_libraries(search_bench PRIVATE search_lib)
    target_compile_options(search_bench PRIVATE ${SEARCH_WARNINGS})
    target_compile_definitions(search_bench PRIVATE TRAINERLAB_DATA_DIR="${TRAINERLAB_DATA_PATH}")

    include(FetchContent)
    FetchContent_Declare(
        Catch2
        GIT_REPOSITORY https://github.com/catchorg/Catch2.git
        GIT_TAG v3.5.4
        GIT_SHALLOW TRUE
    )
    FetchContent_MakeAvailable(Catch2)

    enable_testing()
    add_executable(search_tests tests/test_game.cpp tests/test_forecaster.cpp
                                tests/test_matrix_game.cpp tests/test_mcts.cpp)
    target_link_libraries(search_tests PRIVATE search_lib Catch2::Catch2WithMain)
    target_compile_options(search_tests PRIVATE ${SEARCH_WARNINGS})
    target_compile_definitions(search_tests PRIVATE TRAINERLAB_DATA_DIR="${TRAINERLAB_DATA_PATH}")
    include(Catch)
    catch_discover_tests(search_tests)
endif()
