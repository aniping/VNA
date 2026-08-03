add_library(vna_cpp_compat src/filesystem.cpp)
add_library(vna::cpp_compat ALIAS vna_cpp_compat)

target_compile_features(vna_cpp_compat PUBLIC cxx_std_17)
target_include_directories(
    vna_cpp_compat
    PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_compile_options(vna_cpp_compat PRIVATE -Wall -Wextra -Wpedantic)

# GCC 7 needs stdc++fs; GCC 9 folded filesystem into libstdc++.
if(CMAKE_CXX_COMPILER_VERSION VERSION_LESS 9)
    target_link_libraries(vna_cpp_compat PUBLIC stdc++fs)
endif()
