function(find_pnpm_program output_variable)
    if(WIN32)
        # npm installs an extensionless POSIX shim beside pnpm.cmd. CMake can
        # find that file on Windows but cannot launch it with execute_process.
        find_program(program NAMES pnpm.cmd pnpm.exe REQUIRED NO_CACHE)
    else()
        find_program(program NAMES pnpm REQUIRED NO_CACHE)
    endif()

    set(${output_variable} "${program}" PARENT_SCOPE)
endfunction()
