add_library(vna_observability INTERFACE)
add_library(vna::observability ALIAS vna_observability)

target_compile_features(vna_observability INTERFACE cxx_std_20)
target_include_directories(
    vna_observability
    INTERFACE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
