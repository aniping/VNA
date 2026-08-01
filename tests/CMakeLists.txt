include(GoogleTest)

if(WIN32)
    add_test(
        NAME vna_package_pnpm_discovery_test
        COMMAND
            ${CMAKE_COMMAND}
            -DTEST_TEMP_DIR=${CMAKE_CURRENT_BINARY_DIR}/find-pnpm-test
            -DSOURCE_DIR=${CMAKE_SOURCE_DIR}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/find-pnpm-test.cmake
    )
endif()

add_executable(
    vna_release_launcher_probe
    support/release_launcher_probe.cpp
)
add_test(
    NAME vna_release_launcher_test
    COMMAND
        ${CMAKE_COMMAND}
        -DPROBE=$<TARGET_FILE:vna_release_launcher_probe>
        -DTEST_TEMP_DIR=${CMAKE_CURRENT_BINARY_DIR}/release-launcher-&-test
        -DSOURCE_DIR=${CMAKE_SOURCE_DIR}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/release-launcher-test.cmake
)
set_tests_properties(vna_release_launcher_test PROPERTIES TIMEOUT 10)

add_executable(
    vna_domain_tests
    unit/domain/instrument_test.cpp
)
target_link_libraries(vna_domain_tests PRIVATE vna::domain GTest::gtest_main)
gtest_discover_tests(vna_domain_tests)

add_executable(
    vna_frame_contract_tests
    unit/frames/frame_contract_test.cpp
)
target_link_libraries(
    vna_frame_contract_tests
    PRIVATE
        vna::frame_contracts
        GTest::gtest_main
)
gtest_discover_tests(vna_frame_contract_tests NO_PRETTY_VALUES)

add_executable(
    vna_simulation_tests
    simulation/simulation_visibility_test.cpp
    simulation/simulation_sweep_test.cpp
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

add_executable(
    vna_measurement_tests
    unit/measurement/s_parameter_synthesizer_test.cpp
    unit/measurement/s11_synthesizer_test.cpp
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
    unit/data_plane/log_magnitude_projector_test.cpp
    unit/data_plane/trace_projector_test.cpp
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
    unit/acquisition/continuous_acquisition_hardening_test.cpp
    unit/acquisition/continuous_acquisition_test.cpp
)
target_link_libraries(
    vna_acquisition_tests
    PRIVATE
        vna::acquisition
        GTest::gtest_main
)
target_include_directories(
    vna_acquisition_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/support/include
)
gtest_discover_tests(vna_acquisition_tests)

add_executable(
    vna_display_model_tests
    unit/display_model/display_scale_test.cpp
    unit/display_model/display_workspace_test.cpp
)
target_link_libraries(
    vna_display_model_tests
    PRIVATE
        vna::display_model
        GTest::gtest_main
)
gtest_discover_tests(vna_display_model_tests)

add_executable(
    vna_application_tests
    unit/application/channel_command_test.cpp
    unit/application/command_bus_test.cpp
    unit/application/command_idempotency_test.cpp
    unit/application/command_idempotency_hardening_test.cpp
    unit/application/command_envelope_test.cpp
    unit/application/command_error_test.cpp
    unit/application/completion_fence_test.cpp
    unit/application/completion_fence_hardening_test.cpp
    unit/application/continuous_trace_publisher_test.cpp
    unit/application/continuous_trace_retirement_test.cpp
    unit/application/control_authority_test.cpp
    unit/application/control_authority_concurrency_test.cpp
    unit/application/factory_preset_test.cpp
    unit/application/operation_manager_test.cpp
    unit/application/single_sweep_command_handler_test.cpp
    unit/application/single_sweep_executor_failure_test.cpp
    unit/application/start_single_sweep_command_test.cpp
    unit/application/start_single_sweep_validation_test.cpp
    unit/application/single_sweep_executor_test.cpp
    unit/application/single_sweep_trace_retirement_test.cpp
    unit/application/text_id_test.cpp
    unit/application/trace_display_frame_query_test.cpp
    unit/application/trace_display_frame_query_wait_test.cpp
    unit/application/trace_display_frame_formats_test.cpp
    unit/application/trace_display_frame_query_identity_test.cpp
    unit/application/trace_display_frame_repository_test.cpp
    unit/application/trace_display_frame_set_test.cpp
    unit/application/trace_display_frame_repository_wait_test.cpp
    unit/application/trace_frame_discard_command_test.cpp
    unit/application/trace_command_test.cpp
    unit/application/trace_scale_command_test.cpp
)
target_link_libraries(
    vna_application_tests
    PRIVATE
        vna::application
        vna::simulation
        GTest::gtest_main
)
target_include_directories(
    vna_application_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/support/include
)
gtest_discover_tests(vna_application_tests PROPERTIES TIMEOUT 10)

add_executable(
    vna_platform_contract_tests
    contract/platform/executable_path_contract_test.cpp
    contract/platform/state_directory_contract_test.cpp
)
target_link_libraries(
    vna_platform_contract_tests
    PRIVATE
        vna::platform
        GTest::gtest_main
)
gtest_discover_tests(vna_platform_contract_tests)

add_executable(
    vna_logging_tests
    integration/logging/json_lines_logger_concurrency_test.cpp
    integration/logging/json_lines_logger_test.cpp
    integration/logging/json_lines_logger_overload_test.cpp
    integration/logging/json_lines_logger_rotation_boundary_test.cpp
    integration/logging/json_lines_logger_rotation_test.cpp
)
target_link_libraries(
    vna_logging_tests
    PRIVATE
        vna::logging
        nlohmann_json::nlohmann_json
        GTest::gtest_main
)
gtest_discover_tests(vna_logging_tests PROPERTIES TIMEOUT 10)

add_executable(
    vna_httplib_compatibility_tests
    integration/httplib_compatibility_test.cpp
)
target_link_libraries(
    vna_httplib_compatibility_tests
    PRIVATE
        httplib::httplib
        GTest::gtest_main
)
gtest_discover_tests(
    vna_httplib_compatibility_tests
    PROPERTIES TIMEOUT 10
)

add_executable(
    vna_web_api_tests
    integration/web_api_channel_state_test.cpp
    integration/web_api_display_frame_test.cpp
    integration/web_api_error_test.cpp
    integration/web_api_idempotency_test.cpp
    integration/web_api_operation_test.cpp
    integration/web_api_scale_test.cpp
    integration/web_api_single_sweep_test.cpp
    integration/web_api_static_files_test.cpp
    integration/web_api_text_id_test.cpp
    integration/web_api_test.cpp
    integration/web_api_websocket_latest_test.cpp
    integration/web_api_websocket_lifecycle_test.cpp
    integration/web_api_websocket_test.cpp
)
target_link_libraries(
    vna_web_api_tests
    PRIVATE
        vna::web_api
        vna::simulation
        httplib::httplib
        nlohmann_json::nlohmann_json
        GTest::gtest_main
)
target_include_directories(
    vna_web_api_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/support/include
)
gtest_discover_tests(vna_web_api_tests PROPERTIES TIMEOUT 10)
