include(GoogleTest)

add_executable(
    vna_domain_tests
    unit/domain/instrument_test.cpp
)
target_link_libraries(vna_domain_tests PRIVATE vna::domain GTest::gtest_main)
gtest_discover_tests(vna_domain_tests)

add_executable(
    vna_application_tests
    unit/application/channel_command_test.cpp
    unit/application/command_bus_test.cpp
    unit/application/command_error_test.cpp
    unit/application/trace_command_test.cpp
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
