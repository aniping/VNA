find_package(Threads REQUIRED)

add_library(vna_acquisition src/continuous_acquisition.cpp)
add_library(vna::acquisition ALIAS vna_acquisition)

target_compile_features(vna_acquisition PUBLIC cxx_std_20)
target_include_directories(
    vna_acquisition
    PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_link_libraries(
    vna_acquisition
    PUBLIC vna::raw_receiver_contracts
    PRIVATE Threads::Threads
)
target_compile_options(vna_acquisition PRIVATE -Wall -Wextra -Wpedantic)
