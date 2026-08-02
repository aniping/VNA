add_library(vna_logging
    src/json_line_formatter.cpp
    src/json_lines_logger.cpp
    src/rolling_file.cpp
)
add_library(vna::logging ALIAS vna_logging)

if(WIN32)
    target_sources(vna_logging PRIVATE src/managed_path_windows.cpp)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_sources(vna_logging PRIVATE src/managed_path_linux.cpp)
else()
    message(FATAL_ERROR "Unsupported logging platform")
endif()

target_compile_features(vna_logging PUBLIC cxx_std_20)
target_include_directories(
    vna_logging
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_link_libraries(vna_logging PUBLIC vna::observability
                      PRIVATE nlohmann_json::nlohmann_json spdlog::spdlog)
target_compile_options(vna_logging PRIVATE -Wall -Wextra -Wpedantic)
