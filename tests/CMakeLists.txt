include(GoogleTest)

add_executable(
    vna_domain_tests
    unit/domain/instrument_test.cpp
)
target_link_libraries(vna_domain_tests PRIVATE vna::domain GTest::gtest_main)
gtest_discover_tests(vna_domain_tests)

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
    unit/application/operation_manager_test.cpp
    unit/application/text_id_test.cpp
    unit/application/trace_command_test.cpp
    unit/application/trace_scale_command_test.cpp
)
target_link_libraries(
    vna_application_tests
    PRIVATE
        vna::application
        GTest::gtest_main
)
gtest_discover_tests(vna_application_tests)

add_executable(
    vna_platform_contract_tests
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
    integration/web_api_error_test.cpp
    integration/web_api_idempotency_test.cpp
    integration/web_api_scale_test.cpp
    integration/web_api_text_id_test.cpp
    integration/web_api_test.cpp
)
target_link_libraries(
    vna_web_api_tests
    PRIVATE
        vna::web_api
        httplib::httplib
        nlohmann_json::nlohmann_json
        GTest::gtest_main
)
gtest_discover_tests(vna_web_api_tests PROPERTIES TIMEOUT 10)
