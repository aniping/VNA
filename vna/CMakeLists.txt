set(VNA_ROOT_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
set(VNA_SOURCE_DIR "${VNA_ROOT_DIR}/source")

add_subdirectory(3rdparty)
add_subdirectory(source/runtime)
add_subdirectory(source/adapter)

if(BUILD_TESTING)
    add_subdirectory(source/test)
endif()
