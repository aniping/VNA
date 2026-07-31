add_library(vna_domain src/instrument.cpp)
add_library(vna::domain ALIAS vna_domain)

target_compile_features(vna_domain PUBLIC cxx_std_20)
target_include_directories(
    vna_domain
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

target_compile_options(vna_domain PRIVATE -Wall -Wextra -Wpedantic)
