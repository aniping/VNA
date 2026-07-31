include(GoogleTest)

set(VNA_TEST_SUPPORT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/support")

function(vna_add_test target_name source_file)
    add_executable(${target_name} ${source_file})
    target_include_directories(${target_name} PRIVATE ${VNA_TEST_SUPPORT_DIR})
    target_link_libraries(${target_name} PRIVATE ${ARGN} GTest::gtest_main)
    vna_configure_target(${target_name})
    gtest_discover_tests(${target_name})
endfunction()

add_subdirectory(contract)
add_subdirectory(integration)
