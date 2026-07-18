set(VNA_RUNTIME_HEADERS
    core/base/result.h
    core/base/strong_id.h
    function/instrument/sweep_admission.h
    function/operation/operation_runtime.h
    platform/board/board_port.h
    resource/store/instrument_store.h
)

set(VNA_RUNTIME_SOURCES
    function/instrument/sweep_admission.cpp
    function/operation/operation_runtime.cpp
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
