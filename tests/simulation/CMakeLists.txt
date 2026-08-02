add_executable(
    vna_simulation_tests
    simulation_capture_test.cpp
    simulation_visibility_test.cpp
    simulation_sweep_test.cpp
)
target_link_libraries(
    vna_simulation_tests
    PRIVATE
        vna::simulation
        vna::application
        vna::measurement
        GTest::gtest_main
)
gtest_discover_tests(vna_simulation_tests NO_PRETTY_VALUES)
