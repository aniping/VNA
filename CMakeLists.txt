cmake_minimum_required(VERSION 3.25)

project(VectorNetworkAnalyzer VERSION 0.1.0 LANGUAGES CXX)

option(
    VNA_ENABLE_RELEASE_INSTALL
    "Enable the frozen portable release install rules"
    OFF
)

if(NOT CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    message(FATAL_ERROR "VectorNetworkAnalyzer currently requires GCC on Windows and Linux")
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

include(CTest)
if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
