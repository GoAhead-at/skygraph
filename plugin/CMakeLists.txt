include(FetchContent)

# CommonLibSSE-NG pulled directly via FetchContent (avoids requiring a custom
# vcpkg registry). It brings its own dependencies via its bundled vcpkg.json,
# which our top-level toolchain file picks up.
#
# Tag policy: pin to the latest published release tag (v3.7.0 as of writing,
# https://github.com/CharmedBaryon/CommonLibSSE-NG/tags). To track main for
# bleeding-edge runtime support, override at configure time:
#   cmake --preset msvc-x64-release -DSKYGRAPH_COMMONLIBSSE_REF=main
set(SKYGRAPH_COMMONLIBSSE_REF "v3.7.0" CACHE STRING
    "Git ref of CommonLibSSE-NG to fetch (tag, branch, or commit)")

# Disable CommonLibSSE-NG's bundled unit tests; they pull Catch2 transitively
# and we don't run them as part of skygraph CI. Set as cache vars so they win
# against the option() declarations inside CommonLibSSE-NG.
set(BUILD_TESTS  OFF CACHE BOOL "" FORCE)
set(BUILD_TESTING OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
    CommonLibSSE
    GIT_REPOSITORY https://github.com/CharmedBaryon/CommonLibSSE-NG.git
    GIT_TAG        ${SKYGRAPH_COMMONLIBSSE_REF}
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(CommonLibSSE)

find_package(spdlog CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(ZLIB REQUIRED)
find_package(fmt CONFIG REQUIRED)

set(SKYGRAPH_PLUGIN_SOURCES
    src/plugin.cpp
    src/logging.cpp
    src/config/plugin_config.cpp
    src/transport/pipe_server.cpp
    src/transport/writer_thread.cpp
    src/transport/rolling_recorder.cpp
    src/crash/seh_handler.cpp
    src/samplers/sampler.cpp
    src/samplers/frame_sampler.cpp
    src/samplers/memory_sampler.cpp
    src/samplers/gpu_sampler.cpp
    src/samplers/cpu_breakdown.cpp
    src/samplers/subsystem_hooks.cpp
    src/samplers/papyrus_attribution.cpp
    src/samplers/papyrus_sampler.cpp
    src/samplers/state_sampler.cpp
    src/samplers/event_sources.cpp
    src/samplers/streaming_sampler.cpp
    src/diagnostics/latest_cache.cpp
    src/diagnostics/stutter_flagger.cpp
)

set(SKYGRAPH_PLUGIN_HEADERS
    src/logging.h
    src/config/plugin_config.h
    src/transport/pipe_server.h
    src/transport/writer_thread.h
    src/transport/ring_buffer.h
    src/transport/rolling_recorder.h
    src/crash/seh_handler.h
    src/samplers/sampler.h
    src/samplers/frame_sampler.h
    src/samplers/memory_sampler.h
    src/samplers/gpu_sampler.h
    src/samplers/cpu_breakdown.h
    src/samplers/subsystem_hooks.h
    src/samplers/papyrus_attribution.h
    src/samplers/papyrus_sampler.h
    src/samplers/state_sampler.h
    src/samplers/event_sources.h
    src/samplers/streaming_sampler.h
    src/diagnostics/latest_cache.h
    src/diagnostics/stutter_flagger.h
)

# NOTE: We deliberately do NOT use add_commonlibsse_plugin() here. That helper
# only exists in CommonLibSSE-NG's vcpkg port (it auto-generates a cpp file
# with the SKSEPluginInfo struct from cmake args), and we pull CommonLibSSE-NG
# via FetchContent. We already declare SKSEPluginInfo by hand in plugin.cpp,
# so a plain SHARED library + link is exactly what's needed.
add_library(skygraph SHARED
    ${SKYGRAPH_PLUGIN_SOURCES}
    ${SKYGRAPH_PLUGIN_HEADERS}
)

target_include_directories(skygraph
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(skygraph
    PRIVATE
        CommonLibSSE::CommonLibSSE
        skygraph::protocol
        spdlog::spdlog
        nlohmann_json::nlohmann_json
        ZLIB::ZLIB
        fmt::fmt
        psapi
        dxgi
)

target_precompile_headers(skygraph
    PRIVATE
        <chrono>
        <atomic>
        <thread>
        <mutex>
        <vector>
        <string>
        <string_view>
        <span>
        <memory>
        <optional>
        <nlohmann/json.hpp>
)

# Stage the default config alongside the DLL.
configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/resources/skygraph.json
    ${CMAKE_CURRENT_BINARY_DIR}/skygraph.json
    COPYONLY
)

install(TARGETS skygraph
    LIBRARY DESTINATION SKSE/Plugins
    RUNTIME DESTINATION SKSE/Plugins
)
install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/resources/skygraph.json
    DESTINATION SKSE/Plugins
)
