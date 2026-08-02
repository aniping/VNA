add_library(vna_server_startup_observability startup_observability.cpp)
add_library(vna::server_startup_observability ALIAS
            vna_server_startup_observability)
target_compile_features(vna_server_startup_observability PUBLIC cxx_std_20)
target_include_directories(
    vna_server_startup_observability
    PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_link_libraries(
    vna_server_startup_observability PUBLIC vna::application vna::observability)
target_compile_options(
    vna_server_startup_observability PRIVATE -Wall -Wextra -Wpedantic)

add_executable(vna-server main.cpp)

target_compile_features(vna-server PRIVATE cxx_std_20)
target_link_libraries(
    vna-server
    PRIVATE
        vna::logging
        vna::platform
        vna::server_startup_observability
        vna::simulation
        vna::web_api
)
target_compile_options(vna-server PRIVATE -Wall -Wextra -Wpedantic)
