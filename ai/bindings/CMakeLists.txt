cmake_minimum_required(VERSION 3.20)
project(trainerlab_bindings LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# The engine as a library only: no Catch2 fetched, no demo built.
set(BATTLE_ENGINE_BUILD_TESTS OFF)
set(BATTLE_ENGINE_BUILD_DEMO OFF)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../../engine ${CMAKE_CURRENT_BINARY_DIR}/engine)

find_package(Python 3.10 REQUIRED COMPONENTS Interpreter Development.Module)

# scikit-build-core puts pybind11's config on the path when pybind11 is in build-system.requires;
# a plain CMake build fetches it.
find_package(pybind11 3 CONFIG QUIET)
if(NOT pybind11_FOUND)
    include(FetchContent)
    FetchContent_Declare(
        pybind11
        GIT_REPOSITORY https://github.com/pybind/pybind11.git
        GIT_TAG v3.0.1
        GIT_SHALLOW TRUE
    )
    FetchContent_MakeAvailable(pybind11)
endif()

pybind11_add_module(_engine engine_module.cpp)
target_link_libraries(_engine PRIVATE battle_engine_lib)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(_engine PRIVATE -Wall -Wextra -Wpedantic -Wshadow -Wconversion)
endif()

# Lands as trainerlab/_engine.<ext> in the wheel.
install(TARGETS _engine LIBRARY DESTINATION trainerlab)
