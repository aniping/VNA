add_library(vna_web_api src/json_codec.cpp src/web_api.cpp)
add_library(vna::web_api ALIAS vna_web_api)

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
