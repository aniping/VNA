add_library(vna_display_model src/display_workspace.cpp)
add_library(vna::display_model ALIAS vna_display_model)

target_compile_features(vna_display_model PUBLIC cxx_std_20)
target_include_directories(
    vna_display_model
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_link_libraries(vna_display_model PUBLIC vna::domain)
target_compile_options(vna_display_model PRIVATE -Wall -Wextra -Wpedantic)
