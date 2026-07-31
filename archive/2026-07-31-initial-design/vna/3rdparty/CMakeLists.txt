if(NOT BUILD_TESTING)
    return()
endif()

include(FetchContent)

if(POLICY CMP0135)
    cmake_policy(SET CMP0135 NEW)
endif()

set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
    googletest
    # 依赖归档随仓库保存，确保 MinGW/RTOS 隔离网络环境也能重复配置。
    URL "${CMAKE_CURRENT_LIST_DIR}/packages/googletest-v1.17.0.zip"
    URL_HASH SHA256=40d4ec942217dcc84a9ebe2a68584ada7d4a33a8ee958755763278ea1c5e18ff
)
FetchContent_MakeAvailable(googletest)
