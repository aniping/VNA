set(vna_release_component vna-release)
include("${CMAKE_SOURCE_DIR}/cmake/frontend-build.cmake")
file(GLOB_RECURSE vna_release_assets LIST_DIRECTORIES FALSE
    "${CMAKE_SOURCE_DIR}/frontend/dist/assets/*")
if(NOT EXISTS "${CMAKE_SOURCE_DIR}/frontend/dist/index.html" OR
   NOT vna_release_assets)
    message(FATAL_ERROR
        "Release install requires frontend/dist/index.html and non-empty assets")
endif()

vna_install_frontend_artifacts(
    "${CMAKE_SOURCE_DIR}/frontend" "${vna_release_component}"
)
