find_package(Threads REQUIRED)

add_library(
    vna_application
    src/command_bus.cpp
    src/command_idempotency.cpp
    src/control_authority.cpp
    src/completion_fence.cpp
    src/completion_fence_handles.cpp
    src/continuous_trace_publisher.cpp
    src/factory_preset.cpp
    src/frequency_axis_materialization.cpp
    src/operation_manager.cpp
    src/single_sweep_command_handler.cpp
    src/single_sweep_executor.cpp
    src/single_sweep_operation.cpp
    src/single_sweep_pipeline.cpp
    src/single_sweep_publisher.cpp
    src/single_sweep_trace_registry.cpp
    src/start_single_sweep_command.cpp
    src/trace_display_frame_query.cpp
    src/trace_display_frame_repository.cpp
    src/trace_display_frame_set.cpp
    src/trace_display_frame_set_wait.cpp
    src/trace_display_frame_wait.cpp
    src/trace_format_command.cpp
)
add_library(vna::application ALIAS vna_application)

target_compile_features(vna_application PUBLIC cxx_std_20)
target_include_directories(
    vna_application
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_link_libraries(
    vna_application
    PUBLIC vna::acquisition vna::display_model vna::domain vna::frame_contracts
    PRIVATE Threads::Threads vna::data_plane vna::measurement
)
target_compile_options(vna_application PRIVATE -Wall -Wextra -Wpedantic)
