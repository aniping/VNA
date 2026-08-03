add_library(
    vna_simulation
    src/open_port_leakage.cpp
    src/simulation_capture.cpp
    src/simulation_sweep.cpp
)
add_library(vna::simulation ALIAS vna_simulation)

target_compile_features(vna_simulation PUBLIC cxx_std_17)
target_include_directories(
    vna_simulation
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_link_libraries(
    vna_simulation
    PUBLIC
        vna::acquisition
        vna::cpp_compat
        vna::raw_receiver_contracts
)
target_compile_options(vna_simulation PRIVATE -Wall -Wextra -Wpedantic)
