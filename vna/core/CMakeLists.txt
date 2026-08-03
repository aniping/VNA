find_package(Threads REQUIRED)

add_library(
    vna_application
    control/src/command/all_s_parameters_command.cpp
    control/src/command/command_bus.cpp
    control/src/command/command_configuration_transaction.cpp
    control/src/command/command_idempotency.cpp
    control/src/command/start_single_sweep_command.cpp
    control/src/command/sweep_configuration_command.cpp
    control/src/command/trace_configuration_command.cpp
    control/src/authority/control_authority.cpp
    control/src/operation/completion_fence.cpp
    control/src/operation/completion_fence_handles.cpp
    control/src/operation/operation_manager.cpp
    control/src/preset/factory_preset.cpp
    sweep/src/pipeline/continuous_trace_pipeline.cpp
    sweep/src/pipeline/continuous_trace_publisher.cpp
    sweep/src/pipeline/frequency_axis_materialization.cpp
    sweep/src/pipeline/single_sweep_command_handler.cpp
    sweep/src/pipeline/single_sweep_executor.cpp
    sweep/src/pipeline/single_sweep_operation.cpp
    sweep/src/pipeline/single_sweep_pipeline.cpp
    sweep/src/pipeline/single_sweep_publisher.cpp
    sweep/src/pipeline/single_sweep_trace_registry.cpp
    sweep/src/preview/sweep_preview_assembler.cpp
    sweep/src/preview/sweep_preview_exchange.cpp
    sweep/src/preview/sweep_preview_status.cpp
    sweep/src/preview/sweep_preview_validation.cpp
    sweep/src/runtime/sweep_generation_transaction.cpp
    sweep/src/runtime/sweep_runtime.cpp
    sweep/src/runtime/sweep_runtime_configuration.cpp
    sweep/src/runtime/sweep_runtime_control.cpp
    sweep/src/runtime/sweep_runtime_operation.cpp
    sweep/src/runtime/sweep_runtime_progress.cpp
    sweep/src/runtime/sweep_runtime_restart.cpp
    sweep/src/runtime/sweep_runtime_status.cpp
    sweep/src/runtime/sweep_runtime_worker.cpp
    display/publication/src/trace_display_frame_query.cpp
    display/publication/src/trace_display_frame_repository.cpp
    display/publication/src/trace_display_frame_set.cpp
    display/publication/src/trace_display_frame_set_wait.cpp
    display/publication/src/trace_display_frame_wait.cpp
    display/publication/src/trace_publication_catalog.cpp
)
add_library(vna::application ALIAS vna_application)

target_compile_features(vna_application PUBLIC cxx_std_17)
target_include_directories(
    vna_application
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/control/include>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/sweep/include>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/display/publication/include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/control/src/authority
        ${CMAKE_CURRENT_SOURCE_DIR}/sweep/src/pipeline
        ${CMAKE_CURRENT_SOURCE_DIR}/sweep/src/preview
        ${CMAKE_CURRENT_SOURCE_DIR}/sweep/src/runtime
)
target_link_libraries(
    vna_application
    PUBLIC
        vna::acquisition
        vna::cpp_compat
        vna::display_model
        vna::domain
        vna::frame_contracts
    PRIVATE Threads::Threads spdlog::spdlog vna::data_plane vna::measurement
)
target_compile_options(vna_application PRIVATE -Wall -Wextra -Wpedantic)
