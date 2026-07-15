add_executable(battle_engine_tests
    test_types.cpp
    test_stats.cpp
    test_damage.cpp
    test_status.cpp
    test_engine.cpp
    test_catalog.cpp
    test_validate.cpp
    test_stage.cpp
    test_switch.cpp
    test_ability.cpp
    test_weather.cpp
    test_hazard.cpp
    test_phase8.cpp
    test_phase9.cpp
)

target_include_directories(battle_engine_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(battle_engine_tests PRIVATE battle_engine_lib Catch2::Catch2WithMain)

include(CTest)
include(Catch)
catch_discover_tests(battle_engine_tests)
