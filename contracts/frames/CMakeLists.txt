add_library(vna_frame_contracts src/frames.cpp)
add_library(vna::frame_contracts ALIAS vna_frame_contracts)

target_compile_features(vna_frame_contracts PUBLIC cxx_std_20)
target_include_directories(
    vna_frame_contracts
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_link_libraries(vna_frame_contracts PUBLIC vna::domain)

target_compile_options(vna_frame_contracts PRIVATE -Wall -Wextra -Wpedantic)
