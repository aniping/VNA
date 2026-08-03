function(vna_configure_web_api_test target)
    target_link_libraries(
        ${target}
        PRIVATE
            vna::web_api
            httplib::httplib
            nlohmann_json::nlohmann_json
            GTest::gtest_main
    )
    target_include_directories(
        ${target} PRIVATE ${PROJECT_SOURCE_DIR}/tests/support/include
    )
    gtest_discover_tests(${target} PROPERTIES TIMEOUT 10)
endfunction()

add_executable(
    vna_web_api_http_tests
    ../web_api_all_s_parameters_test.cpp
    ../web_api_channel_state_test.cpp
    ../web_api_error_test.cpp
    ../web_api_idempotency_test.cpp
    ../web_api_operation_test.cpp
    ../web_api_scale_test.cpp
    ../web_api_state_cache_test.cpp
    ../web_api_trace_measurement_test.cpp
    ../web_api_text_id_test.cpp
    ../web_api_test.cpp
)
vna_configure_web_api_test(vna_web_api_http_tests)

add_executable(
    vna_web_api_business_log_tests
    ../web_api_business_log_test.cpp
)
vna_configure_web_api_test(vna_web_api_business_log_tests)
target_link_libraries(vna_web_api_business_log_tests PRIVATE spdlog::spdlog)

add_executable(
    vna_web_api_display_tests
    ../web_api_display_frame_test.cpp
    ../web_api_single_sweep_test.cpp
)
vna_configure_web_api_test(vna_web_api_display_tests)
target_link_libraries(vna_web_api_display_tests PRIVATE vna::simulation)

add_executable(
    vna_web_api_static_tests
    ../web_api_static_files_test.cpp
)
vna_configure_web_api_test(vna_web_api_static_tests)

add_executable(
    vna_web_api_websocket_tests
    ../web_api_sweep_preview_test.cpp
    ../web_api_websocket_frame_set_test.cpp
    ../web_api_websocket_latest_test.cpp
    ../web_api_websocket_lifecycle_test.cpp
    ../web_api_websocket_test.cpp
)
vna_configure_web_api_test(vna_web_api_websocket_tests)

# The established target remains a convenient full Web API build entry point.
add_custom_target(
    vna_web_api_tests
    DEPENDS
        vna_web_api_display_tests
        vna_web_api_business_log_tests
        vna_web_api_http_tests
        vna_web_api_static_tests
        vna_web_api_websocket_tests
)
