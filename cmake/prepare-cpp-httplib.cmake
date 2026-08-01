function(vna_prepare_patched_cpp_httplib target)
    find_package(Git REQUIRED)

    set(source_header "${PROJECT_SOURCE_DIR}/third-part/cpp-httplib/httplib.h")
    set(patch_file
        "${PROJECT_SOURCE_DIR}/cmake/patches/cpp-httplib-websocket-close-now.patch")
    set(patched_directory "${CMAKE_BINARY_DIR}/generated/cpp-httplib")
    set(patched_header "${patched_directory}/httplib.h")

    file(SHA256 "${source_header}" source_hash)
    if(NOT source_hash STREQUAL
            "3b476e13b519fa1f2476da1bcea8d8051e7bcb3cee0c40f7a05ee776a373a449")
        message(FATAL_ERROR
            "cpp-httplib version changed; close_now patch requires review")
    endif()
    file(MAKE_DIRECTORY "${patched_directory}")
    configure_file("${source_header}" "${patched_header}" COPYONLY)
    file(RELATIVE_PATH patch_root "${PROJECT_SOURCE_DIR}" "${patched_directory}")
    file(TO_CMAKE_PATH "${patch_root}" patch_root)

    execute_process(
        COMMAND
            "${GIT_EXECUTABLE}" apply --check --unidiff-zero --unsafe-paths
            "--directory=${patch_root}" "${patch_file}"
        WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
        RESULT_VARIABLE check_result
        ERROR_VARIABLE check_error
    )
    if(NOT check_result EQUAL 0)
        message(FATAL_ERROR
            "cpp-httplib close_now patch no longer applies:\n${check_error}")
    endif()
    execute_process(
        COMMAND
            "${GIT_EXECUTABLE}" apply --unidiff-zero --unsafe-paths
            "--directory=${patch_root}" "${patch_file}"
        WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
        RESULT_VARIABLE apply_result
        ERROR_VARIABLE apply_error
    )
    if(NOT apply_result EQUAL 0)
        message(FATAL_ERROR
            "Failed to patch generated cpp-httplib header:\n${apply_error}")
    endif()

    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
        "${source_header}" "${patch_file}")
    # The original submodule include remains as an upstream target detail. A
    # BEFORE build interface makes every consumer resolve our generated header.
    target_include_directories(${target} SYSTEM BEFORE INTERFACE
        "$<BUILD_INTERFACE:${patched_directory}>")
endfunction()
