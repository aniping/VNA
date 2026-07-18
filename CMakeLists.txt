cmake_minimum_required(VERSION 3.20)

project(vna_upper_software VERSION 0.1.0 LANGUAGES CXX)

if(WIN32 AND NOT MINGW)
    message(FATAL_ERROR "Windows development builds must use MinGW-w64")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(target_config)
include(CTest)

add_subdirectory(vna)
