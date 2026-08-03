include_guard(GLOBAL)
include("${CMAKE_CURRENT_LIST_DIR}/find-pnpm.cmake")
function(vna_add_frontend_build frontend_dir)
    find_program(node_program NAMES node.exe node REQUIRED NO_CACHE)
    find_pnpm_program(pnpm_program)
    file(GLOB_RECURSE frontend_source_inputs
        CONFIGURE_DEPENDS LIST_DIRECTORIES FALSE
        "${frontend_dir}/src/*" "${frontend_dir}/public/*")
    file(GLOB frontend_config_inputs CONFIGURE_DEPENDS LIST_DIRECTORIES FALSE
        "${frontend_dir}/*.json" "${frontend_dir}/*.ts"
        "${frontend_dir}/*.html" "${frontend_dir}/*.yaml")
    set(frontend_inputs ${frontend_source_inputs} ${frontend_config_inputs})
    set(stamp "${CMAKE_BINARY_DIR}/frontend/vna-frontend.stamp")
    add_custom_command(
        OUTPUT "${stamp}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_BINARY_DIR}/frontend"
        COMMAND "${pnpm_program}" run build
        COMMAND "${CMAKE_COMMAND}" -E touch "${stamp}"
        DEPENDS ${frontend_inputs}
        BYPRODUCTS "${frontend_dir}/dist/index.html"
        WORKING_DIRECTORY "${frontend_dir}"
        USES_TERMINAL
        VERBATIM
    )
    add_custom_target(vna-frontend ALL DEPENDS "${stamp}")
    message(STATUS "Frontend build enabled with ${pnpm_program}")
    vna_install_frontend_artifacts("${frontend_dir}" vna-frontend)
endfunction()
function(vna_install_frontend_artifacts frontend_dir component)
    install(FILES "${frontend_dir}/dist/index.html"
        DESTINATION web COMPONENT ${component})
    install(DIRECTORY "${frontend_dir}/dist/assets/"
        DESTINATION web/assets COMPONENT ${component})
endfunction()
