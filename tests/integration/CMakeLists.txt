add_executable(
    vna_logging_tests
    logging/json_lines_logger_concurrency_test.cpp
    logging/json_lines_logger_rotation_boundary_test.cpp
    logging/json_lines_logger_rotation_test.cpp
    logging/json_lines_logger_test.cpp
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
    httplib_compatibility_test.cpp
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

add_subdirectory(web-api)
