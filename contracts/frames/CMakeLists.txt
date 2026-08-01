add_library(vna_raw_receiver_contracts INTERFACE)
add_library(vna::raw_receiver_contracts ALIAS vna_raw_receiver_contracts)
target_compile_features(vna_raw_receiver_contracts INTERFACE cxx_std_20)
target_include_directories(
    vna_raw_receiver_contracts
    INTERFACE $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

add_library(vna_frame_contracts src/frames.cpp)
add_library(vna::frame_contracts ALIAS vna_frame_contracts)

target_link_libraries(vna_frame_contracts PUBLIC vna::domain vna::raw_receiver_contracts)

target_compile_options(vna_frame_contracts PRIVATE -Wall -Wextra -Wpedantic)
