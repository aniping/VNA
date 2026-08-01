cmake_minimum_required(VERSION 3.25)

if(NOT DEFINED TEST_TEMP_DIR OR NOT DEFINED SOURCE_DIR)
    message(FATAL_ERROR "TEST_TEMP_DIR and SOURCE_DIR are required")
endif()

file(MAKE_DIRECTORY "${TEST_TEMP_DIR}")
file(WRITE "${TEST_TEMP_DIR}/pnpm" "#!/bin/sh\n")
file(WRITE "${TEST_TEMP_DIR}/pnpm.cmd" "@echo off\r\n")
set(CMAKE_PROGRAM_PATH "${TEST_TEMP_DIR}")

include("${SOURCE_DIR}/cmake/find-pnpm.cmake")
find_pnpm_program(pnpm_program)

file(TO_CMAKE_PATH "${TEST_TEMP_DIR}/pnpm.cmd" expected_program)
if(NOT pnpm_program STREQUAL expected_program)
    message(FATAL_ERROR
        "Expected Windows command shim ${expected_program}, got ${pnpm_program}")
endif()
