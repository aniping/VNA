cmake_minimum_required(VERSION 3.25)

if(NOT DEFINED RELEASE_ROOT)
    message(FATAL_ERROR "RELEASE_ROOT is required")
endif()
get_filename_component(RELEASE_ROOT "${RELEASE_ROOT}" ABSOLUTE)

function(require_directory path)
    if(IS_SYMLINK "${path}" OR NOT IS_DIRECTORY "${path}")
        message(FATAL_ERROR "Required release directory is invalid: ${path}")
    endif()
endfunction()

function(require_file path)
    if(IS_SYMLINK "${path}" OR NOT EXISTS "${path}" OR IS_DIRECTORY "${path}")
        message(FATAL_ERROR "Required release file is invalid: ${path}")
    endif()
endfunction()

function(require_entries directory expected)
    file(GLOB entries RELATIVE "${directory}" "${directory}/*")
    list(SORT entries)
    set(sorted_expected ${expected})
    list(SORT sorted_expected)
    if(NOT "${entries}" STREQUAL "${sorted_expected}")
        message(FATAL_ERROR
            "Unexpected release entries in ${directory}: ${entries}")
    endif()
endfunction()

function(require_managed_logs directory)
    file(GLOB entries RELATIVE "${directory}" "${directory}/*")
    foreach(entry IN LISTS entries)
        if(NOT entry MATCHES "^vna(\\.[1-4])?\\.log$")
            message(FATAL_ERROR "Unexpected release log entry: ${entry}")
        endif()
        require_file("${directory}/${entry}")
    endforeach()
endfunction()

require_directory("${RELEASE_ROOT}")
require_directory("${RELEASE_ROOT}/bin")
require_directory("${RELEASE_ROOT}/logs")
require_directory("${RELEASE_ROOT}/web")
require_directory("${RELEASE_ROOT}/web/assets")
require_file("${RELEASE_ROOT}/web/index.html")
require_file("${RELEASE_ROOT}/README.txt")
require_entries("${RELEASE_ROOT}/web" "assets;index.html")
require_managed_logs("${RELEASE_ROOT}/logs")

if(EXISTS "${RELEASE_ROOT}/bin/vna-server.exe")
    set(server "${RELEASE_ROOT}/bin/vna-server.exe")
    set(launcher start.cmd)
    set(top_entries "README.txt;bin;logs;start.cmd;web")
    require_file("${RELEASE_ROOT}/${launcher}")
    file(GET_RUNTIME_DEPENDENCIES
        EXECUTABLES "${server}"
        DIRECTORIES "${RELEASE_ROOT}/bin"
        UNRESOLVED_DEPENDENCIES_VAR unresolved
        PRE_EXCLUDE_REGEXES
            "^(api-ms-|ext-ms-).*"
            "^(kernel32|msvcrt|ws2_32|advapi32|bcrypt)\\.dll$"
        POST_EXCLUDE_REGEXES
            ".*[Ww][Ii][Nn][Dd][Oo][Ww][Ss][/\\\\](System32|SysWOW64)[/\\\\].*"
    )
    if(unresolved)
        message(FATAL_ERROR "Unresolved Windows runtime dependencies: ${unresolved}")
    endif()
    file(GLOB bin_entries RELATIVE "${RELEASE_ROOT}/bin" "${RELEASE_ROOT}/bin/*")
    foreach(entry IN LISTS bin_entries)
        if(NOT entry STREQUAL "vna-server.exe" AND NOT entry MATCHES "\\.dll$")
            message(FATAL_ERROR "Unexpected Windows bin entry: ${entry}")
        endif()
    endforeach()
elseif(EXISTS "${RELEASE_ROOT}/bin/vna-server")
    set(server "${RELEASE_ROOT}/bin/vna-server")
    set(launcher start.sh)
    set(top_entries "README.txt;bin;logs;start.sh;web")
    require_file("${RELEASE_ROOT}/${launcher}")
    require_entries("${RELEASE_ROOT}/bin" "vna-server")
else()
    message(FATAL_ERROR "Release server executable is missing")
endif()

require_file("${server}")
require_entries("${RELEASE_ROOT}" "${top_entries}")
file(GLOB_RECURSE assets LIST_DIRECTORIES false "${RELEASE_ROOT}/web/assets/*")
if(NOT assets)
    message(FATAL_ERROR "Release assets directory is empty")
endif()
