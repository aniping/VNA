set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
add_subdirectory(googletest EXCLUDE_FROM_ALL)

set(HTTPLIB_INSTALL OFF CACHE BOOL "" FORCE)
set(HTTPLIB_TEST OFF CACHE BOOL "" FORCE)
set(HTTPLIB_USE_OPENSSL_IF_AVAILABLE OFF CACHE BOOL "" FORCE)
set(HTTPLIB_USE_WOLFSSL_IF_AVAILABLE OFF CACHE BOOL "" FORCE)
set(HTTPLIB_USE_MBEDTLS_IF_AVAILABLE OFF CACHE BOOL "" FORCE)
set(HTTPLIB_USE_ZLIB_IF_AVAILABLE OFF CACHE BOOL "" FORCE)
set(HTTPLIB_USE_BROTLI_IF_AVAILABLE OFF CACHE BOOL "" FORCE)
set(HTTPLIB_USE_ZSTD_IF_AVAILABLE OFF CACHE BOOL "" FORCE)
set(HTTPLIB_USE_NON_BLOCKING_GETADDRINFO OFF CACHE BOOL "" FORCE)
add_subdirectory(cpp-httplib EXCLUDE_FROM_ALL)
include(${PROJECT_SOURCE_DIR}/cmake/prepare-cpp-httplib.cmake)
vna_prepare_patched_cpp_httplib(httplib)

set(JSON_BuildTests OFF CACHE BOOL "" FORCE)
set(JSON_Install OFF CACHE BOOL "" FORCE)
add_subdirectory(nlohmann-json EXCLUDE_FROM_ALL)

# Keep the logging dependency compiled and private; examples, tests, benchmarks,
# and install rules belong to upstream and are not part of this product build.
set(SPDLOG_BUILD_SHARED OFF CACHE BOOL "" FORCE)
set(SPDLOG_BUILD_EXAMPLE OFF CACHE BOOL "" FORCE)
set(SPDLOG_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(SPDLOG_BUILD_BENCH OFF CACHE BOOL "" FORCE)
set(SPDLOG_INSTALL OFF CACHE BOOL "" FORCE)
add_subdirectory(spdlog EXCLUDE_FROM_ALL)

get_target_property(VNA_SPDLOG_TYPE spdlog TYPE)
if(NOT VNA_SPDLOG_TYPE STREQUAL "STATIC_LIBRARY")
    message(FATAL_ERROR "VectorNetworkAnalyzer requires compiled static spdlog")
endif()
