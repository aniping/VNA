add_library(vna_server_runtime_log STATIC runtime_log.cpp)
target_compile_features(vna_server_runtime_log PRIVATE cxx_std_17)
target_include_directories(vna_server_runtime_log PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(
    vna_server_runtime_log
    PRIVATE spdlog::spdlog vna::cpp_compat
)
target_compile_options(
    vna_server_runtime_log
    PRIVATE -Wall -Wextra -Wpedantic -finput-charset=UTF-8 -fexec-charset=UTF-8
)

add_executable(vna-server main.cpp)

target_compile_features(vna-server PRIVATE cxx_std_17)
target_link_libraries(
    vna-server
    PRIVATE
        vna::platform
        vna::simulation
        vna::web_api
        vna_server_runtime_log
        spdlog::spdlog
)
target_compile_options(
    vna-server
    PRIVATE -Wall -Wextra -Wpedantic -finput-charset=UTF-8 -fexec-charset=UTF-8
)
