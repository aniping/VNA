add_executable(vna-server main.cpp)

target_compile_features(vna-server PRIVATE cxx_std_20)
target_link_libraries(vna-server PRIVATE vna::web_api)
target_compile_options(vna-server PRIVATE -Wall -Wextra -Wpedantic)
