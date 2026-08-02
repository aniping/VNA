add_library(
    vna_measurement
    src/s11_synthesizer.cpp
    src/s_parameter_range_synthesizer.cpp
)
add_library(vna::measurement ALIAS vna_measurement)

target_compile_features(vna_measurement PUBLIC cxx_std_20)
target_include_directories(
    vna_measurement
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_link_libraries(vna_measurement PUBLIC vna::frame_contracts)
target_compile_options(vna_measurement PRIVATE -Wall -Wextra -Wpedantic)
