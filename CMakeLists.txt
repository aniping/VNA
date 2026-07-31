cmake_minimum_required(VERSION 3.25)

project(VectorNetworkAnalyzer VERSION 0.1.0 LANGUAGES CXX)

if(NOT CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    message(FATAL_ERROR "VectorNetworkAnalyzer currently requires GCC on Windows and Linux")
endif()

set(CMAKE_CXX_EXTENSIONS OFF)

add_subdirectory(third-part)
add_subdirectory(core/domain)
add_subdirectory(core/application)
add_subdirectory(infrastructure/platform)
add_subdirectory(protocols/web-api)
add_subdirectory(apps/vna-server)

include(CTest)
if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
