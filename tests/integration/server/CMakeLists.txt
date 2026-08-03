add_executable(
    vna_runtime_log_probe
    runtime_log_probe.cpp
)
target_compile_features(vna_runtime_log_probe PRIVATE cxx_std_17)
target_include_directories(
    vna_runtime_log_probe
    PRIVATE ${PROJECT_SOURCE_DIR}/apps/vna-server
)
target_link_libraries(
    vna_runtime_log_probe
    PRIVATE
        vna::cpp_compat
        vna_server_runtime_log
        spdlog::spdlog
)
if(WIN32)
    target_link_options(vna_runtime_log_probe PRIVATE -municode)
endif()

add_test(
    NAME vna_runtime_log_test
    COMMAND
        ${CMAKE_COMMAND}
        -DPROBE=$<TARGET_FILE:vna_runtime_log_probe>
        -DTEST_TEMP_DIR=${CMAKE_CURRENT_BINARY_DIR}/runtime-log-test
        -P ${CMAKE_CURRENT_SOURCE_DIR}/runtime-log-test.cmake
)
set_tests_properties(vna_runtime_log_test PROPERTIES TIMEOUT 10)
