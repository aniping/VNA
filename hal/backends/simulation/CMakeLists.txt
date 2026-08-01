add_library(vna_simulation src/simulation_sweep.cpp)
add_library(vna::simulation ALIAS vna_simulation)

target_compile_features(vna_simulation PUBLIC cxx_std_20)
target_include_directories(
    vna_simulation
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_link_libraries(vna_simulation PUBLIC vna::raw_receiver_contracts)
target_compile_options(vna_simulation PRIVATE -Wall -Wextra -Wpedantic)
