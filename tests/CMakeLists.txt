include(GoogleTest)

add_executable(
    vna_domain_tests
    unit/domain/instrument_test.cpp
)
target_link_libraries(vna_domain_tests PRIVATE vna::domain GTest::gtest_main)
gtest_discover_tests(vna_domain_tests)

add_executable(
    vna_application_tests
    unit/application/command_bus_test.cpp
)
target_link_libraries(
    vna_application_tests
    PRIVATE
        vna::application
        GTest::gtest_main
)
gtest_discover_tests(vna_application_tests)
