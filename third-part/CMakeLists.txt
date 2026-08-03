include(${PROJECT_SOURCE_DIR}/cmake/prepare-third-party.cmake)

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

set(JSON_BuildTests OFF CACHE BOOL "" FORCE)
set(JSON_Install OFF CACHE BOOL "" FORCE)
add_subdirectory(nlohmann-json EXCLUDE_FROM_ALL)

# The runtime log uses the compiled static library only. Upstream examples,
# tests and install rules are unrelated to this product build.
set(SPDLOG_BUILD_ALL OFF CACHE BOOL "" FORCE)
set(SPDLOG_BUILD_SHARED OFF CACHE BOOL "" FORCE)
set(SPDLOG_BUILD_EXAMPLE OFF CACHE BOOL "" FORCE)
set(SPDLOG_BUILD_EXAMPLE_HO OFF CACHE BOOL "" FORCE)
set(SPDLOG_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(SPDLOG_BUILD_TESTS_HO OFF CACHE BOOL "" FORCE)
set(SPDLOG_BUILD_BENCH OFF CACHE BOOL "" FORCE)
set(SPDLOG_INSTALL OFF CACHE BOOL "" FORCE)
if(WIN32)
    # Wide filenames preserve Unicode release paths. The console option lets
    # the Windows sink convert UTF-8 messages without relying on chcp.
    set(SPDLOG_WCHAR_FILENAMES ON CACHE BOOL "" FORCE)
    set(SPDLOG_WCHAR_CONSOLE ON CACHE BOOL "" FORCE)
endif()
add_subdirectory(spdlog EXCLUDE_FROM_ALL)

set(VNA_SPDLOG_LEVEL_NAMES
    "{spdlog::string_view_t(\"TRACE\",5),spdlog::string_view_t(\"DEBUG\",5),spdlog::string_view_t(\"INFO\",4),spdlog::string_view_t(\"WARN\",4),spdlog::string_view_t(\"ERROR\",5),spdlog::string_view_t(\"CRITICAL\",8),spdlog::string_view_t(\"OFF\",3)}"
)
target_compile_definitions(
    spdlog PUBLIC "SPDLOG_LEVEL_NAMES=${VNA_SPDLOG_LEVEL_NAMES}"
)

get_target_property(VNA_SPDLOG_TYPE spdlog TYPE)
if(NOT VNA_SPDLOG_TYPE STREQUAL "STATIC_LIBRARY")
    message(FATAL_ERROR "VectorNetworkAnalyzer requires compiled static spdlog")
endif()
