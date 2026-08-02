cmake_minimum_required(VERSION 3.25)

foreach(required IN ITEMS PROBE TEST_TEMP_DIR)
    if(NOT DEFINED ${required})
        message(FATAL_ERROR "${required} is required")
    endif()
endforeach()

file(REMOVE_RECURSE "${TEST_TEMP_DIR}")
set(release_root "${TEST_TEMP_DIR}/发布目录")
file(MAKE_DIRECTORY "${release_root}")

function(run_probe root output_var error_var result_var)
    execute_process(
        COMMAND "${PROBE}" "${root}"
        RESULT_VARIABLE result
        OUTPUT_VARIABLE output
        ERROR_VARIABLE error
        ENCODING UTF-8
    )
    set(${output_var} "${output}" PARENT_SCOPE)
    set(${error_var} "${error}" PARENT_SCOPE)
    set(${result_var} "${result}" PARENT_SCOPE)
endfunction()

run_probe("${release_root}" console errors result)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "Runtime log probe failed: ${errors}")
endif()
set(log_file "${release_root}/logs/vna.log")
if(NOT EXISTS "${log_file}")
    message(FATAL_ERROR "Runtime log file was not created")
endif()
file(READ "${log_file}" contents)
if(NOT console STREQUAL contents)
    message(FATAL_ERROR "Console and file text differ")
endif()
string(REPLACE "\r\n" "\n" normalized "${contents}")
string(REGEX REPLACE "\n$" "" normalized "${normalized}")
string(REPLACE "\n" ";" lines "${normalized}")
set(timestamp
    "[0-9][0-9][0-9][0-9]-[0-9][0-9]-[0-9][0-9] [0-9][0-9]:[0-9][0-9]:[0-9][0-9]\\.[0-9][0-9][0-9]"
)
set(expected_lines
    "DEBUG    调试消息"
    "INFO     服务启动完成"
    "WARN     警告消息"
    "ERROR    错误消息"
)
list(LENGTH lines line_count)
if(NOT line_count EQUAL 4)
    message(FATAL_ERROR "Runtime log must contain exactly four lines")
endif()
foreach(index RANGE 0 3)
    list(GET lines ${index} line)
    list(GET expected_lines ${index} expected)
    if(NOT line MATCHES "^${timestamp}  ${expected}$")
        message(FATAL_ERROR "Runtime log line has the wrong shape: ${line}")
    endif()
endforeach()
if(contents MATCHES "\\{.*\\}" OR EXISTS "${release_root}/logs/vna.jsonl")
    message(FATAL_ERROR "Runtime log unexpectedly contains JSON output")
endif()

set(fallback_root "${TEST_TEMP_DIR}/fallback")
file(MAKE_DIRECTORY "${fallback_root}")
file(WRITE "${fallback_root}/logs" "occupied")
run_probe("${fallback_root}" fallback_console fallback_errors fallback_result)
if(NOT fallback_result EQUAL 0)
    message(FATAL_ERROR "Console-only fallback did not keep running")
endif()
if(NOT fallback_console MATCHES "无法写入日志文件，将仅输出到控制台" OR
   NOT fallback_console MATCHES "INFO     服务启动完成")
    message(FATAL_ERROR "Console-only warning or later log is missing")
endif()
if(NOT fallback_errors MATCHES "Runtime log file is unavailable")
    message(FATAL_ERROR "Console-only fallback did not warn on stderr")
endif()
