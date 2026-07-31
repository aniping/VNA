add_library(vna_data_plane src/log_magnitude_projector.cpp)
add_library(vna::data_plane ALIAS vna_data_plane)

target_compile_features(vna_data_plane PUBLIC cxx_std_20)
target_include_directories(
    vna_data_plane
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_link_libraries(vna_data_plane PUBLIC vna::frame_contracts)
target_compile_options(vna_data_plane PRIVATE -Wall -Wextra -Wpedantic)
