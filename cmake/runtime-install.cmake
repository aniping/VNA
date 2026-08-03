if(WIN32)
    get_filename_component(vna_compiler_bin "${CMAKE_CXX_COMPILER}" DIRECTORY)
    install(
        TARGETS vna-server
        RUNTIME_DEPENDENCIES
            DIRECTORIES "${vna_compiler_bin}"
            PRE_EXCLUDE_REGEXES
                "^(api-ms-|ext-ms-).*"
                "^(kernel32|msvcrt|ws2_32|advapi32|bcrypt)\\.dll$"
            POST_EXCLUDE_REGEXES
                ".*[Ww][Ii][Nn][Dd][Oo][Ww][Ss][/\\\\](System32|SysWOW64)[/\\\\].*"
        RUNTIME DESTINATION bin COMPONENT vna-backend
    )
    set(vna_install_launcher start.cmd)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    install(
        TARGETS vna-server
        RUNTIME DESTINATION bin COMPONENT vna-backend
    )
    set(vna_install_launcher start.sh)
else()
    message(FATAL_ERROR "Unsupported install platform")
endif()
install(DIRECTORY DESTINATION logs COMPONENT vna-backend)
install(PROGRAMS "${CMAKE_SOURCE_DIR}/packaging/${vna_install_launcher}"
    DESTINATION . COMPONENT vna-backend)
install(FILES "${CMAKE_SOURCE_DIR}/packaging/README.txt"
    DESTINATION . COMPONENT vna-backend)
