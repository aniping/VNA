if(WIN32)
    set(
        vna_platform_sources
        windows/executable_path.cpp
        windows/state_directory.cpp
    )
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(
        vna_platform_sources
        linux/executable_path.cpp
        linux/state_directory.cpp
    )
else()
    message(FATAL_ERROR "Unsupported platform")
endif()

add_library(vna_platform ${vna_platform_sources})
add_library(vna::platform ALIAS vna_platform)

target_compile_features(vna_platform PUBLIC cxx_std_20)
target_include_directories(
    vna_platform
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_compile_options(vna_platform PRIVATE -Wall -Wextra -Wpedantic)
