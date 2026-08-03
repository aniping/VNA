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
add_subdirectory(foundation/cpp-compat)
add_subdirectory(core/instrument)
add_subdirectory(contracts/frames)
add_subdirectory(core/measurement)
add_subdirectory(core/display/projection)
add_subdirectory(core/acquisition)
add_subdirectory(core/display/model)
add_subdirectory(core)
add_subdirectory(hardware/backends/simulation)
add_subdirectory(infrastructure/platform)
add_subdirectory(interfaces/web)
add_subdirectory(apps/vna-server)

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
