cmake_minimum_required(VERSION 3.25)

get_filename_component(source_candidate "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
file(REAL_PATH "${source_candidate}" source_dir)
set(build_dir "${source_dir}/out/release-package")
set(release_parent "${source_dir}/release")
set(final_dir "${release_parent}/VectorNetworkAnalyzer")
string(RANDOM LENGTH 12 ALPHABET 0123456789abcdef stage_id)
set(stage_dir "${release_parent}/.VectorNetworkAnalyzer-stage-${stage_id}")
set(backup_dir "${release_parent}/.VectorNetworkAnalyzer-backup")

function(validate_release_parent)
    if(IS_SYMLINK "${release_parent}")
        message(FATAL_ERROR "Release parent must not be a symbolic link")
    endif()
    if(EXISTS "${release_parent}")
        if(NOT IS_DIRECTORY "${release_parent}")
            message(FATAL_ERROR "Release parent must be a directory")
        endif()
        file(REAL_PATH "${release_parent}" resolved_parent)
        if(NOT resolved_parent STREQUAL release_parent)
            message(FATAL_ERROR "Release parent escapes the source tree")
        endif()
    endif()
endfunction()

function(private_path_is_safe path result)
    get_filename_component(parent "${path}" DIRECTORY)
    if(NOT parent STREQUAL release_parent)
        set(${result} FALSE PARENT_SCOPE)
        return()
    endif()
    if(IS_SYMLINK "${path}")
        set(${result} FALSE PARENT_SCOPE)
        return()
    endif()
    if(EXISTS "${path}")
        file(REAL_PATH "${path}" resolved)
        get_filename_component(normalized "${path}" ABSOLUTE)
        if(NOT resolved STREQUAL normalized)
            set(${result} FALSE PARENT_SCOPE)
            return()
        endif()
    endif()
    set(${result} TRUE PARENT_SCOPE)
endfunction()

function(assert_private_path path)
    private_path_is_safe("${path}" path_is_safe)
    if(NOT path_is_safe)
        message(FATAL_ERROR "Refusing unsafe release path: ${path}")
    endif()
endfunction()

foreach(path IN ITEMS "${final_dir}" "${stage_dir}" "${backup_dir}")
    assert_private_path("${path}")
endforeach()

function(remove_private_tree path)
    assert_private_path("${path}")
    if(EXISTS "${path}")
        file(REMOVE_RECURSE "${path}")
    endif()
    if(EXISTS "${path}" OR IS_SYMLINK "${path}")
        message(FATAL_ERROR "Failed to remove managed release path: ${path}")
    endif()
endfunction()

function(remove_stale_stages)
    file(GLOB stale_stages LIST_DIRECTORIES TRUE
        "${release_parent}/.VectorNetworkAnalyzer-stage-*")
    foreach(stale_stage IN LISTS stale_stages)
        # Only exact children with the private prefix reach deletion. The path
        # gate still rejects links and junctions instead of following them.
        remove_private_tree("${stale_stage}")
    endforeach()
endfunction()

function(run_step label working_directory)
    execute_process(
        COMMAND ${ARGN}
        WORKING_DIRECTORY "${working_directory}"
        RESULT_VARIABLE result
    )
    if(NOT result EQUAL 0)
        remove_private_tree("${stage_dir}")
        message(FATAL_ERROR "${label} failed with exit code ${result}")
    endif()
endfunction()

validate_release_parent()
remove_stale_stages()
if(EXISTS "${backup_dir}" OR IS_SYMLINK "${backup_dir}")
    assert_private_path("${backup_dir}")
    if(EXISTS "${final_dir}" OR IS_SYMLINK "${final_dir}")
        assert_private_path("${final_dir}")
        run_step("existing release validation" "${source_dir}"
            "${CMAKE_COMMAND}" "-DRELEASE_ROOT=${final_dir}"
            -DALLOW_EXISTING_LOGS=ON
            -P "${source_dir}/cmake/verify-release-layout.cmake")
        remove_private_tree("${backup_dir}")
    else()
        file(RENAME "${backup_dir}" "${final_dir}" RESULT restore_result)
        if(NOT restore_result STREQUAL "0")
            message(FATAL_ERROR "Cannot restore parked release: ${restore_result}")
        endif()
        run_step("restored release validation" "${source_dir}"
            "${CMAKE_COMMAND}" "-DRELEASE_ROOT=${final_dir}"
            -DALLOW_EXISTING_LOGS=ON
            -P "${source_dir}/cmake/verify-release-layout.cmake")
    endif()
endif()

include("${source_dir}/cmake/find-pnpm.cmake")
find_pnpm_program(pnpm_program)
find_program(ninja_program NAMES ninja REQUIRED)
find_program(cxx_compiler NAMES g++ REQUIRED)

# Frontend dist and the CMake build tree are disposable intermediates. Nothing
# below touches the final release until install and validation both succeed.
run_step("frontend build" "${source_dir}/frontend"
    "${pnpm_program}" run build)
run_step("release configure" "${source_dir}"
    "${CMAKE_COMMAND}" -S "${source_dir}" -B "${build_dir}"
    -G Ninja
    "-DCMAKE_MAKE_PROGRAM=${ninja_program}"
    "-DCMAKE_CXX_COMPILER=${cxx_compiler}"
    -DCMAKE_BUILD_TYPE=Release
    -DBUILD_TESTING=OFF
    -DVNA_ENABLE_RELEASE_INSTALL=ON)
run_step("server build" "${source_dir}"
    "${CMAKE_COMMAND}" --build "${build_dir}" --target vna-server)

file(MAKE_DIRECTORY "${release_parent}")
validate_release_parent()
run_step("release install" "${source_dir}"
    "${CMAKE_COMMAND}" --install "${build_dir}"
    --prefix "${stage_dir}" --component vna-release)
run_step("release validation" "${source_dir}"
    "${CMAKE_COMMAND}" "-DRELEASE_ROOT=${stage_dir}"
    -P "${source_dir}/cmake/verify-release-layout.cmake")

# Directory rename stays on one filesystem. If promotion fails after parking an
# older release, restore it before reporting failure; never expose staged files.
if(EXISTS "${backup_dir}" OR IS_SYMLINK "${backup_dir}")
    remove_private_tree("${stage_dir}")
    assert_private_path("${backup_dir}")
    message(FATAL_ERROR "Unexpected release backup appeared during packaging")
endif()
set(had_previous FALSE)
if(EXISTS "${final_dir}" OR IS_SYMLINK "${final_dir}")
    private_path_is_safe("${final_dir}" final_is_safe)
    if(NOT final_is_safe)
        remove_private_tree("${stage_dir}")
        message(FATAL_ERROR "Final release path changed during packaging")
    endif()
    run_step("existing release validation" "${source_dir}"
        "${CMAKE_COMMAND}" "-DRELEASE_ROOT=${final_dir}"
        -DALLOW_EXISTING_LOGS=ON
        -P "${source_dir}/cmake/verify-release-layout.cmake")
    file(RENAME "${final_dir}" "${backup_dir}" RESULT park_result)
    if(NOT park_result STREQUAL "0")
        remove_private_tree("${stage_dir}")
        message(FATAL_ERROR "Cannot park previous release: ${park_result}")
    endif()
    set(had_previous TRUE)
endif()

file(RENAME "${stage_dir}" "${final_dir}" RESULT promote_result)
if(NOT promote_result STREQUAL "0")
    if(had_previous)
        file(RENAME "${backup_dir}" "${final_dir}" RESULT restore_result)
        if(NOT restore_result STREQUAL "0")
            remove_private_tree("${stage_dir}")
            message(FATAL_ERROR
                "Promotion and restore failed: ${promote_result}; ${restore_result}")
        endif()
    endif()
    remove_private_tree("${stage_dir}")
    message(FATAL_ERROR "Cannot promote staged release: ${promote_result}")
endif()

remove_private_tree("${backup_dir}")
message(STATUS "Release ready: ${final_dir}")
