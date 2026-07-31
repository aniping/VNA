include(GoogleTest)

add_executable(
    vna_domain_tests
    unit/domain/instrument_test.cpp
)
target_link_libraries(vna_domain_tests PRIVATE vna::domain GTest::gtest_main)
gtest_discover_tests(vna_domain_tests)
