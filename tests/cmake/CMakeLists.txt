if(WIN32)
    add_test(
        NAME vna_package_pnpm_discovery_test
        COMMAND
            ${CMAKE_COMMAND}
            -DTEST_TEMP_DIR=${CMAKE_CURRENT_BINARY_DIR}/find-pnpm-test
            -DSOURCE_DIR=${PROJECT_SOURCE_DIR}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/find-pnpm-test.cmake
    )
endif()
