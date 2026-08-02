function(vna_configure_application_test target)
    target_link_libraries(${target} PRIVATE vna::application GTest::gtest_main)
    target_include_directories(
        ${target} PRIVATE ${PROJECT_SOURCE_DIR}/tests/support/include
    )
    gtest_discover_tests(${target} PROPERTIES TIMEOUT 10)
endfunction()

add_executable(
    vna_application_command_tests
    channel_command_test.cpp
    command_bus_test.cpp
    command_bus_catalog_dependency_test.cpp
    command_idempotency_test.cpp
    command_idempotency_hardening_test.cpp
    command_envelope_test.cpp
    command_error_test.cpp
    control_authority_test.cpp
    control_authority_concurrency_test.cpp
    text_id_test.cpp
)
vna_configure_application_test(vna_application_command_tests)

add_executable(
    vna_application_operation_tests
    completion_fence_test.cpp
    completion_fence_hardening_test.cpp
    operation_manager_test.cpp
)
vna_configure_application_test(vna_application_operation_tests)

add_executable(
    vna_application_sweep_tests
    continuous_trace_failure_test.cpp
    continuous_trace_log_test.cpp
    continuous_trace_publisher_test.cpp
    continuous_trace_plan_test.cpp
    continuous_trace_dynamic_test.cpp
    continuous_trace_retirement_test.cpp
    factory_preset_test.cpp
    single_sweep_command_handler_test.cpp
    single_sweep_executor_failure_test.cpp
    start_single_sweep_command_test.cpp
    start_single_sweep_validation_test.cpp
    single_sweep_executor_test.cpp
    single_sweep_trace_retirement_test.cpp
)
vna_configure_application_test(vna_application_sweep_tests)
target_link_libraries(
    vna_application_sweep_tests PRIVATE spdlog::spdlog vna::simulation
)

add_executable(
    vna_application_sweep_preview_tests
    sweep_preview_exchange_test.cpp
    sweep_preview_validation_test.cpp
)
vna_configure_application_test(vna_application_sweep_preview_tests)

add_executable(
    vna_application_trace_frame_tests
    trace_display_frame_query_test.cpp
    trace_display_frame_query_wait_test.cpp
    trace_display_frame_formats_test.cpp
    trace_display_frame_query_identity_test.cpp
    trace_display_frame_repository_test.cpp
    trace_display_frame_set_test.cpp
    trace_display_frame_set_wait_test.cpp
    trace_publication_catalog_test.cpp
    trace_publication_catalog_concurrency_test.cpp
    trace_publication_catalog_validation_test.cpp
    trace_display_frame_repository_wait_test.cpp
)
vna_configure_application_test(vna_application_trace_frame_tests)

add_executable(
    vna_application_trace_command_tests
    all_s_parameters_command_test.cpp
    all_s_parameters_command_validation_test.cpp
    trace_frame_discard_command_test.cpp
    trace_command_test.cpp
    trace_configuration_transaction_test.cpp
    trace_measurement_type_command_test.cpp
    trace_measurement_type_command_hardening_test.cpp
    trace_measurement_type_command_concurrency_test.cpp
    trace_scale_command_test.cpp
)
vna_configure_application_test(vna_application_trace_command_tests)

# Keep the established target as an aggregate while focused builds link only one
# deep-module executable.
add_custom_target(
    vna_application_tests
    DEPENDS
        vna_application_command_tests
        vna_application_operation_tests
        vna_application_sweep_preview_tests
        vna_application_sweep_tests
        vna_application_trace_command_tests
        vna_application_trace_frame_tests
)
