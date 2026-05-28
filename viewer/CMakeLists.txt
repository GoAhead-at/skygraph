find_package(imgui CONFIG REQUIRED)
find_package(implot CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)
find_package(fmt CONFIG REQUIRED)
find_package(ZLIB REQUIRED)

set(SKYGRAPH_VIEWER_SOURCES
    src/main.cpp
    src/app.cpp
    src/transport/pipe_client.cpp
    src/transport/ndjson_source.cpp
    src/transport/file_source.cpp
    src/state/telemetry_store.cpp
    src/panels/status_bar.cpp
    src/panels/charts_panel.cpp
    src/panels/papyrus_panel.cpp
    src/panels/events_panel.cpp
    src/panels/plugins_panel.cpp
    src/panels/stutter_panel.cpp
    src/panels/timeline_panel.cpp
)

set(SKYGRAPH_VIEWER_HEADERS
    src/app.h
    src/transport/pipe_client.h
    src/transport/ndjson_source.h
    src/transport/file_source.h
    src/state/telemetry_store.h
    src/panels/status_bar.h
    src/panels/charts_panel.h
    src/panels/papyrus_panel.h
    src/panels/events_panel.h
    src/panels/plugins_panel.h
    src/panels/stutter_panel.h
    src/panels/timeline_panel.h
)

add_executable(skygraph_viewer WIN32
    ${SKYGRAPH_VIEWER_SOURCES}
    ${SKYGRAPH_VIEWER_HEADERS}
)
set_target_properties(skygraph_viewer PROPERTIES
    OUTPUT_NAME "skygraph"
)

target_include_directories(skygraph_viewer
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(skygraph_viewer
    PRIVATE
        skygraph::protocol
        imgui::imgui
        implot::implot
        nlohmann_json::nlohmann_json
        spdlog::spdlog
        fmt::fmt
        ZLIB::ZLIB
        d3d11
        dxgi
        d3dcompiler
        dwmapi
        comdlg32
)

target_compile_features(skygraph_viewer PRIVATE cxx_std_23)

target_precompile_headers(skygraph_viewer
    PRIVATE
        <atomic>
        <chrono>
        <memory>
        <mutex>
        <string>
        <string_view>
        <thread>
        <vector>
        <deque>
        <nlohmann/json.hpp>
)

# Stage the default ImGui layout next to the exe so first-run UX is right.
configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/resources/imgui.ini.default
    ${CMAKE_CURRENT_BINARY_DIR}/imgui.ini.default
    COPYONLY
)

install(TARGETS skygraph_viewer RUNTIME DESTINATION viewer)
install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/resources/imgui.ini.default
        DESTINATION viewer)
