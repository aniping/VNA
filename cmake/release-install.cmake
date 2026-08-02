set(vna_release_component vna-release)

if(WIN32)
    get_filename_component(
        vna_compiler_bin "${CMAKE_CXX_COMPILER}" DIRECTORY
    )
    # Resolve the built PE's actual dependencies instead of guessing which
    # MinGW exception/thread model supplied this compiler.
    install(
        TARGETS vna-server
        RUNTIME_DEPENDENCIES
            DIRECTORIES "${vna_compiler_bin}"
            PRE_EXCLUDE_REGEXES
                "^(api-ms-|ext-ms-).*"
                "^(kernel32|msvcrt|ws2_32|advapi32|bcrypt)\\.dll$"
            POST_EXCLUDE_REGEXES
                ".*[Ww][Ii][Nn][Dd][Oo][Ww][Ss][/\\\\](System32|SysWOW64)[/\\\\].*"
        RUNTIME DESTINATION bin COMPONENT ${vna_release_component}
    )
    set(vna_release_launcher start.cmd)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    install(
        TARGETS vna-server
        RUNTIME DESTINATION bin COMPONENT ${vna_release_component}
    )
    set(vna_release_launcher start.sh)
else()
    message(FATAL_ERROR "Unsupported release platform")
endif()

install(
    FILES "${CMAKE_SOURCE_DIR}/frontend/dist/index.html"
    DESTINATION web
    COMPONENT ${vna_release_component}
)
install(
    DIRECTORY "${CMAKE_SOURCE_DIR}/frontend/dist/assets/"
    DESTINATION web/assets
    COMPONENT ${vna_release_component}
)
# Runtime data is created only by the explicit portable install. Ordinary
# builds remain intermediates and never create a release log directory.
install(
    DIRECTORY
    DESTINATION logs
    COMPONENT ${vna_release_component}
)
install(
    PROGRAMS "${CMAKE_SOURCE_DIR}/packaging/${vna_release_launcher}"
    DESTINATION .
    COMPONENT ${vna_release_component}
)
install(
    FILES "${CMAKE_SOURCE_DIR}/packaging/README.txt"
    DESTINATION .
    COMPONENT ${vna_release_component}
)
