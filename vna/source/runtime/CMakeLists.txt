set(VNA_RUNTIME_HEADERS
    core/base/result.h
    core/base/strong_id.h
    function/acquisition/acquisition_admission.h
    function/acquisition/acquisition_engine.h
    function/acquisition/acquisition_result.h
    function/instrument/instrument_kernel.h
    function/instrument/sweep_admission.h
    function/operation/operation_runtime.h
    platform/board/board_execution_reservation.h
    platform/board/board_prepare_drain_owner.h
    platform/board/board_port.h
    resource/store/instrument_store.h
)

set(VNA_RUNTIME_SOURCES
    function/acquisition/acquisition_admission.cpp
    function/acquisition/acquisition_engine.cpp
    function/instrument/instrument_kernel.cpp
    function/instrument/sweep_admission.cpp
    function/operation/operation_runtime.cpp
    platform/board/board_execution_reservation.cpp
    platform/board/board_prepare_drain_owner.cpp
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
