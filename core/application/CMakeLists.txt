add_library(vna_application src/command_bus.cpp)
add_library(vna::application ALIAS vna_application)

target_compile_features(vna_application PUBLIC cxx_std_20)
target_include_directories(
    vna_application
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_link_libraries(vna_application PUBLIC vna::display_model vna::domain)
target_compile_options(vna_application PRIVATE -Wall -Wextra -Wpedantic)
