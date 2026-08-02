add_library(
    vna_web_api
    src/command_json_decoder.cpp
    src/command_outcome_info.cpp
    src/display_frame_http_handler.cpp
    src/display_frame_json_codec.cpp
    src/display_frame_stream.cpp
    src/display_frame_stream_lifecycle.cpp
    src/json_codec.cpp
    src/operation_http_handler.cpp
    src/web_asset_path.cpp
    src/web_api.cpp
)
add_library(vna::web_api ALIAS vna_web_api)

if(WIN32)
    target_sources(vna_web_api PRIVATE src/web_asset_path_windows.cpp)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_sources(vna_web_api PRIVATE src/web_asset_path_linux.cpp)
else()
    message(FATAL_ERROR "Unsupported web asset platform")
endif()

target_compile_features(vna_web_api PUBLIC cxx_std_20)
target_include_directories(
    vna_web_api
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_link_libraries(
    vna_web_api
    PUBLIC
        vna::application
    PRIVATE
        httplib::httplib
        nlohmann_json::nlohmann_json
)
target_compile_options(vna_web_api PRIVATE -Wall -Wextra -Wpedantic)
