cmake_minimum_required(VERSION 3.20)

project(vna_upper_software VERSION 0.1.0 LANGUAGES CXX)

if(WIN32 AND NOT MINGW)
    message(FATAL_ERROR "Windows development builds must use MinGW-w64")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(vna_board_mock STATIC
    src/board/mock_board.cpp
)
target_include_directories(vna_board_mock PUBLIC include)
target_compile_options(vna_board_mock PRIVATE
    $<$<CXX_COMPILER_ID:GNU>:-Wall -Wextra -Wpedantic -Werror>
)

add_library(vna_runtime STATIC
    src/runtime/operation_runtime.cpp
)
target_include_directories(vna_runtime PUBLIC include)
target_compile_options(vna_runtime PRIVATE
    $<$<CXX_COMPILER_ID:GNU>:-Wall -Wextra -Wpedantic -Werror>
)

add_library(vna_store STATIC
    src/store/instrument_store.cpp
)
target_include_directories(vna_store PUBLIC include)
target_compile_options(vna_store PRIVATE
    $<$<CXX_COMPILER_ID:GNU>:-Wall -Wextra -Wpedantic -Werror>
)

add_library(vna_instrument STATIC
    src/instrument/sweep_admission.cpp
)
target_include_directories(vna_instrument PUBLIC include)
target_link_libraries(vna_instrument PUBLIC vna_runtime vna_store)
target_compile_options(vna_instrument PRIVATE
    $<$<CXX_COMPILER_ID:GNU>:-Wall -Wextra -Wpedantic -Werror>
)

include(CTest)
if(BUILD_TESTING)
    add_executable(vna_board_contract_tests
        tests/board_adapter_contract_tests.cpp
    )
    target_link_libraries(vna_board_contract_tests PRIVATE vna_board_mock)
    target_compile_options(vna_board_contract_tests PRIVATE
        $<$<CXX_COMPILER_ID:GNU>:-Wall -Wextra -Wpedantic -Werror>
    )
    add_test(NAME vna_board_contract_tests COMMAND vna_board_contract_tests)

    add_executable(vna_runtime_contract_tests
        tests/runtime_contract_tests.cpp
    )
    target_link_libraries(vna_runtime_contract_tests PRIVATE vna_runtime)
    target_compile_options(vna_runtime_contract_tests PRIVATE
        $<$<CXX_COMPILER_ID:GNU>:-Wall -Wextra -Wpedantic -Werror>
    )
    add_test(NAME vna_runtime_contract_tests COMMAND vna_runtime_contract_tests)

    add_executable(vna_store_contract_tests
        tests/store_contract_tests.cpp
    )
    target_link_libraries(vna_store_contract_tests PRIVATE vna_store)
    target_compile_options(vna_store_contract_tests PRIVATE
        $<$<CXX_COMPILER_ID:GNU>:-Wall -Wextra -Wpedantic -Werror>
    )
    add_test(NAME vna_store_contract_tests COMMAND vna_store_contract_tests)

    add_executable(vna_cross_layer_contract_tests
        tests/cross_layer_contract_tests.cpp
    )
    target_link_libraries(vna_cross_layer_contract_tests PRIVATE vna_instrument)
    target_compile_options(vna_cross_layer_contract_tests PRIVATE
        $<$<CXX_COMPILER_ID:GNU>:-Wall -Wextra -Wpedantic -Werror>
    )
    add_test(NAME vna_cross_layer_contract_tests COMMAND vna_cross_layer_contract_tests)
endif()
