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
    src/encoding.cpp
    src/vec_env.cpp
    src/network.cpp
    src/planner.cpp
    src/self_play.cpp
)
target_include_directories(search_lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
find_package(Threads REQUIRED)
target_link_libraries(search_lib PUBLIC battle_engine_lib Threads::Threads)
target_compile_options(search_lib PRIVATE ${SEARCH_WARNINGS})
# Same reason as the engine: a search that sums probabilities must rank alike on macOS and Linux.
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(search_lib PRIVATE -ffp-contract=off)
endif()
set_target_properties(search_lib PROPERTIES POSITION_INDEPENDENT_CODE ON)

# Palier 8.2: the network's matrix products through BLAS. On by default on macOS, where it is
# Accelerate (the M4's matrix unit); off elsewhere, where search::multiply does them.
if(APPLE)
    set(SEARCH_BLAS_DEFAULT ON)
else()
    set(SEARCH_BLAS_DEFAULT OFF)
endif()
option(SEARCH_BLAS "Multiply the network's layers with BLAS (Accelerate on macOS)" ${SEARCH_BLAS_DEFAULT})
if(SEARCH_BLAS)
    target_compile_definitions(search_lib PRIVATE SEARCH_BLAS)
    if(APPLE)
        target_link_libraries(search_lib PUBLIC "-framework Accelerate")
    else()
        find_package(BLAS REQUIRED)
        target_link_libraries(search_lib PUBLIC BLAS::BLAS)
    endif()
endif()

if(SEARCH_BUILD_TESTS)
    set(TRAINERLAB_DATA_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../engine/data")

    add_executable(search_bench bench/bench.cpp)
    target_link_libraries(search_bench PRIVATE search_lib)
    target_compile_options(search_bench PRIVATE ${SEARCH_WARNINGS})
    target_compile_definitions(search_bench PRIVATE TRAINERLAB_DATA_DIR="${TRAINERLAB_DATA_PATH}")
    if(APPLE)
        target_link_libraries(search_bench PRIVATE "-framework Accelerate")
    endif()

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
                                tests/test_matrix_game.cpp tests/test_mcts.cpp
                                tests/test_encoding.cpp tests/test_vec_env.cpp
                                tests/test_planner.cpp tests/test_self_play.cpp)
    target_link_libraries(search_tests PRIVATE search_lib Catch2::Catch2WithMain)
    target_compile_options(search_tests PRIVATE ${SEARCH_WARNINGS})
    target_compile_definitions(search_tests PRIVATE TRAINERLAB_DATA_DIR="${TRAINERLAB_DATA_PATH}")
    include(Catch)
    catch_discover_tests(search_tests)
endif()
