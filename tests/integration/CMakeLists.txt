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
