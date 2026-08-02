add_executable(vna_domain_tests domain/instrument_test.cpp)
target_link_libraries(vna_domain_tests PRIVATE vna::domain GTest::gtest_main)
gtest_discover_tests(vna_domain_tests)

add_executable(vna_frame_contract_tests frames/frame_contract_test.cpp)
target_link_libraries(
    vna_frame_contract_tests
    PRIVATE
        vna::frame_contracts
        GTest::gtest_main
)
gtest_discover_tests(vna_frame_contract_tests NO_PRETTY_VALUES)

add_executable(
    vna_measurement_tests
    measurement/s_parameter_range_synthesizer_test.cpp
    measurement/s_parameter_synthesizer_test.cpp
    measurement/s11_synthesizer_test.cpp
)
target_link_libraries(
    vna_measurement_tests
    PRIVATE
        vna::measurement
        GTest::gtest_main
)
gtest_discover_tests(vna_measurement_tests)

add_executable(
    vna_data_plane_tests
    data_plane/log_magnitude_projector_test.cpp
    data_plane/trace_projector_test.cpp
)
target_link_libraries(
    vna_data_plane_tests
    PRIVATE
        vna::data_plane
        GTest::gtest_main
)
gtest_discover_tests(vna_data_plane_tests)

add_executable(
    vna_acquisition_tests
    acquisition/continuous_acquisition_hardening_test.cpp
    acquisition/continuous_acquisition_test.cpp
    acquisition/raw_sweep_capture_test.cpp
)
target_link_libraries(
    vna_acquisition_tests
    PRIVATE
        vna::acquisition
        GTest::gtest_main
)
target_include_directories(
    vna_acquisition_tests PRIVATE ${PROJECT_SOURCE_DIR}/tests/support/include
)
gtest_discover_tests(vna_acquisition_tests)

add_executable(
    vna_display_model_tests
    display_model/display_initial_scale_test.cpp
    display_model/display_scale_test.cpp
    display_model/display_workspace_test.cpp
)
target_link_libraries(
    vna_display_model_tests
    PRIVATE
        vna::display_model
        GTest::gtest_main
)
gtest_discover_tests(vna_display_model_tests)

add_subdirectory(application)
