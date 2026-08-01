add_executable(
    vna_release_launcher_probe
    release_launcher_probe.cpp
)
add_test(
    NAME vna_release_launcher_test
    COMMAND
        ${CMAKE_COMMAND}
        -DPROBE=$<TARGET_FILE:vna_release_launcher_probe>
        -DTEST_TEMP_DIR=${CMAKE_CURRENT_BINARY_DIR}/release-launcher-&-test
        -DSOURCE_DIR=${PROJECT_SOURCE_DIR}
        -P ${PROJECT_SOURCE_DIR}/tests/cmake/release-launcher-test.cmake
)
set_tests_properties(vna_release_launcher_test PROPERTIES TIMEOUT 10)
