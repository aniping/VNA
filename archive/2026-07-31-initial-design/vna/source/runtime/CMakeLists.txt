set(VNA_RUNTIME_HEADERS
    core/base/result.h
    core/base/strong_id.h
    function/acquisition/a_only_completion_owners.h
    function/acquisition/acquisition_admission.h
    function/acquisition/acquisition_drain_owner.h
    function/acquisition/acquisition_engine.h
    function/acquisition/acquisition_ingress.h
    function/acquisition/acquisition_result.h
    function/acquisition/candidate_commit_lease.h
    function/acquisition/network_observation_builder.h
    function/acquisition/network_observation_error.h
    function/instrument/instrument_kernel.h
    function/instrument/sweep_admission.h
    function/operation/operation_runtime.h
    platform/board/acquisition_buffer_pool.h
    platform/board/board_execution_reservation.h
    platform/board/board_prepare_drain_owner.h
    platform/board/board_port.h
    resource/store/completed_sweep_bundle.h
    resource/store/instrument_store.h
)

set(VNA_RUNTIME_SOURCES
    function/acquisition/a_only_completion_owners.cpp
    function/acquisition/acquisition_admission.cpp
    function/acquisition/acquisition_drain_owner.cpp
    function/acquisition/acquisition_engine.cpp
    function/acquisition/acquisition_ingress.cpp
    function/acquisition/candidate_commit_lease.cpp
    function/acquisition/network_observation_builder.cpp
    function/instrument/instrument_kernel.cpp
    function/instrument/sweep_admission.cpp
    function/operation/operation_runtime.cpp
    platform/board/acquisition_buffer_pool.cpp
    platform/board/board_execution_reservation.cpp
    platform/board/board_prepare_drain_owner.cpp
    platform/board/board_port.cpp
    resource/store/completed_sweep_bundle.cpp
    resource/store/instrument_store.cpp
)

add_library(VnaRuntime STATIC
    ${VNA_RUNTIME_HEADERS}
    ${VNA_RUNTIME_SOURCES}
)
add_library(vna::runtime ALIAS VnaRuntime)

target_include_directories(VnaRuntime PUBLIC
    $<BUILD_INTERFACE:${VNA_SOURCE_DIR}>
)
vna_configure_target(VnaRuntime)
