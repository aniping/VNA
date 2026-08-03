cmake_minimum_required(VERSION 3.25)

option(VNA_BUILD_FRONTEND "Build the Vue frontend" ON)
option(VNA_BUILD_BACKEND "Build the C++ backend" ON)

option(
    VNA_ENABLE_RELEASE_INSTALL
    "Enable the frozen portable release install rules"
    OFF
)

project(VectorNetworkAnalyzer VERSION 0.1.0 LANGUAGES NONE)

include(cmake/frontend-build.cmake)
if(VNA_BUILD_FRONTEND)
    vna_add_frontend_build("${CMAKE_SOURCE_DIR}/frontend")
endif()

if(NOT VNA_BUILD_BACKEND)
    if(NOT VNA_BUILD_FRONTEND)
        message(FATAL_ERROR "At least one of frontend or backend must be enabled")
    endif()
    return()
endif()

enable_language(CXX)
if(NOT CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    message(FATAL_ERROR
        "VectorNetworkAnalyzer currently requires GCC on Windows and Linux")
endif()

set(CMAKE_CXX_EXTENSIONS OFF)

add_subdirectory(third-part)
add_subdirectory(vna/foundation/cpp-compat)
add_subdirectory(vna/core/instrument)
add_subdirectory(vna/contracts/frames)
add_subdirectory(vna/core/measurement)
add_subdirectory(vna/core/display/projection)
add_subdirectory(vna/core/acquisition)
add_subdirectory(vna/core/display/model)
add_subdirectory(vna/core)
add_subdirectory(vna/hardware/backends/simulation)
add_subdirectory(vna/infrastructure/platform)
add_subdirectory(vna/interfaces/web)
add_subdirectory(vna/apps/vna-server)

include(cmake/runtime-install.cmake)
if(VNA_ENABLE_RELEASE_INSTALL)
    include(cmake/release-install.cmake)
endif()

# This target is intentionally outside ALL. Ordinary builds stay intermediate;
# only an explicit request may replace release/VectorNetworkAnalyzer.
add_custom_target(
    package-release
    COMMAND ${CMAKE_COMMAND} -P ${CMAKE_SOURCE_DIR}/cmake/package-release.cmake
    USES_TERMINAL
    COMMENT "Build and validate the portable release directory"
    VERBATIM
)
if(TARGET vna-frontend)
    add_dependencies(package-release vna-frontend)
endif()

include(CTest)
if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
