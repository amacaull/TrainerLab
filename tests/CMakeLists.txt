add_executable(battle_engine_tests
    test_types.cpp
    test_stats.cpp
    test_damage.cpp
    test_engine.cpp
)

target_link_libraries(battle_engine_tests PRIVATE battle_engine_lib Catch2::Catch2WithMain)

include(CTest)
include(Catch)
catch_discover_tests(battle_engine_tests)
