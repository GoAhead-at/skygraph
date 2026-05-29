cmake_minimum_required(VERSION 3.21)

project(
    skygraph
    VERSION 0.1.1
    DESCRIPTION "Real-time Skyrim telemetry for diagnosing stutter"
    LANGUAGES CXX
)

# C++23 across the board (required by CommonLibSSE-NG and used by viewer too)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Static MD runtime, matches CommonLibSSE-NG's /MD ABI
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")

# Group artifacts conveniently
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

if(MSVC)
    add_compile_options(
        /W4
        /permissive-
        /Zc:__cplusplus
        /Zc:preprocessor
        /utf-8
        /MP
        /EHsc
    )
    add_compile_definitions(
        WIN32_LEAN_AND_MEAN
        NOMINMAX
        _WIN32_WINNT=0x0A00   # Windows 10+
        UNICODE
        _UNICODE
    )
endif()

# Shared protocol headers (consumed by plugin + viewer)
add_subdirectory(protocol)

# SKSE plugin DLL
add_subdirectory(plugin)

# Standalone viewer exe
add_subdirectory(viewer)

# Packaging: combined target that drives `cmake --install` into a staging
# directory and then invokes 7z to produce both release archives.
find_program(SKYGRAPH_7Z 7z)
add_custom_target(skygraph_package
    COMMENT "Stage install tree + build skygraph-plugin-*.7z / skygraph-viewer-*.zip"
    COMMAND ${CMAKE_COMMAND} -E rm -rf "${CMAKE_BINARY_DIR}/stage"
    COMMAND ${CMAKE_COMMAND} --install ${CMAKE_BINARY_DIR}
            --config $<CONFIG>
            --prefix "${CMAKE_BINARY_DIR}/stage"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_SOURCE_DIR}/dist"
)
if(SKYGRAPH_7Z)
    add_custom_command(TARGET skygraph_package POST_BUILD
        COMMAND ${SKYGRAPH_7Z} a -mx=9
                "${CMAKE_SOURCE_DIR}/dist/skygraph-plugin-v${PROJECT_VERSION}.7z"
                "${CMAKE_BINARY_DIR}/stage/SKSE"
        COMMAND ${SKYGRAPH_7Z} a -mx=9
                "${CMAKE_SOURCE_DIR}/dist/skygraph-viewer-v${PROJECT_VERSION}.zip"
                "${CMAKE_BINARY_DIR}/stage/viewer/*"
        VERBATIM
    )
else()
    add_custom_command(TARGET skygraph_package POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E echo
            "SKIPPED: 7z not found on PATH. See docs/packaging.md."
    )
endif()
add_dependencies(skygraph_package skygraph skygraph_viewer)

# Build summary
message(STATUS "")
message(STATUS "=== skygraph ${PROJECT_VERSION} ===")
message(STATUS "  C++ standard:     ${CMAKE_CXX_STANDARD}")
message(STATUS "  MSVC runtime:     ${CMAKE_MSVC_RUNTIME_LIBRARY}")
message(STATUS "  vcpkg triplet:    ${VCPKG_TARGET_TRIPLET}")
message(STATUS "  Build type:       ${CMAKE_BUILD_TYPE}")
message(STATUS "  7z found:         ${SKYGRAPH_7Z}")
message(STATUS "")
