add_executable(battle_engine_tests
    test_types.cpp
    test_stats.cpp
    test_damage.cpp
    test_engine.cpp
    test_catalog.cpp
    test_validate.cpp
    test_status.cpp
    test_stages.cpp
    test_switch.cpp
    test_abilities.cpp
    test_weather.cpp
    test_hazards.cpp
    test_accuracy_crits.cpp
    test_protect_multiturn.cpp
    test_pp_struggle.cpp
    test_items.cpp
    test_field.cpp
    test_move_mechanics.cpp
    test_integration.cpp
    test_ffi.cpp
)

target_link_libraries(battle_engine_tests PRIVATE battle_engine_lib Catch2::Catch2WithMain)

target_compile_definitions(battle_engine_tests PRIVATE
    BATTLE_ENGINE_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
)

include(CTest)
include(Catch)
catch_discover_tests(battle_engine_tests)
