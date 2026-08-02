cmake_minimum_required(VERSION 3.25)

foreach(required IN ITEMS PROBE TEST_TEMP_DIR SOURCE_DIR)
    if(NOT DEFINED ${required})
        message(FATAL_ERROR "${required} is required")
    endif()
endforeach()

file(MAKE_DIRECTORY
    "${TEST_TEMP_DIR}/release/bin"
    "${TEST_TEMP_DIR}/release/logs"
    "${TEST_TEMP_DIR}/outside"
)

if(WIN32)
    set(launcher "${TEST_TEMP_DIR}/release/start.cmd")
    set(server "${TEST_TEMP_DIR}/release/bin/vna-server.exe")
    # A stable wrapper quotes the environment-expanded path inside cmd itself;
    # CMake's automatic .cmd invocation otherwise reparses '&' as an operator.
    set(wrapper "${CMAKE_CURRENT_BINARY_DIR}/release-launcher-wrapper.cmd")
    file(WRITE "${wrapper}" "@call \"%VNA_TEST_LAUNCHER%\"\r\n")
    set(command cmd /d /c "${wrapper}")
    file(COPY_FILE "${SOURCE_DIR}/packaging/start.cmd" "${launcher}")
else()
    set(launcher "${TEST_TEMP_DIR}/release/start.sh")
    set(server "${TEST_TEMP_DIR}/release/bin/vna-server")
    set(command /bin/sh "${launcher}")
    file(COPY_FILE "${SOURCE_DIR}/packaging/start.sh" "${launcher}")
endif()
file(COPY_FILE "${PROBE}" "${server}")
if(NOT WIN32)
    file(CHMOD "${server}" PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE)
endif()

function(run_launcher exit_code output_var result_var)
    execute_process(
        COMMAND ${CMAKE_COMMAND} -E env
            "VNA_TEST_EXIT_CODE=${exit_code}"
            "VNA_TEST_LAUNCHER=${launcher}" ${command}
        WORKING_DIRECTORY "${TEST_TEMP_DIR}/outside"
        RESULT_VARIABLE result
        OUTPUT_VARIABLE output
        ERROR_VARIABLE error
    )
    set(${output_var} "${output}${error}" PARENT_SCOPE)
    set(${result_var} "${result}" PARENT_SCOPE)
endfunction()

function(require_contains output expected)
    string(FIND "${output}" "${expected}" found)
    if(found EQUAL -1)
        message(FATAL_ERROR "Launcher output misses '${expected}': ${output}")
    endif()
endfunction()

run_launcher(0 success_output success_result)
if(NOT success_result EQUAL 0)
    message(FATAL_ERROR
        "Successful launcher returned ${success_result}: ${success_output}")
endif()
require_contains("${success_output}" "Starting Vector Network Analyzer")
require_contains("${success_output}" "Web URL: http://127.0.0.1:8080/")
require_contains("${success_output}" "Text log:")
require_contains("${success_output}" "Structured log:")
if(success_output MATCHES "\"event\"|server\.lifecycle")
    message(FATAL_ERROR "Launcher exposed structured JSON: ${success_output}")
endif()

run_launcher(7 failure_output failure_result)
if(NOT failure_result EQUAL 7)
    message(FATAL_ERROR "Launcher did not preserve exit code 7: ${failure_result}")
endif()
require_contains("${failure_output}" "ERROR: Vector Network Analyzer exited")
require_contains("${failure_output}" "Text log:")
