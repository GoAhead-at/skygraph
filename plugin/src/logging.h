#pragma once

#include <spdlog/spdlog.h>
#include <spdlog/sinks/basic_file_sink.h>

#include <filesystem>
#include <string_view>

namespace skygraph::logging {

// Wires up spdlog's default logger to a file under the SKSE log directory.
// If a_subDir is non-empty, the log is placed in that subdirectory (created
// if needed) so it sits alongside the recorder's session files instead of
// loose in the SKSE root. Idempotent; safe to call once at SKSEPluginLoad.
void Init(std::string_view a_pluginName, spdlog::level::level_enum a_level,
          std::string_view a_subDir = {});

}  // namespace skygraph::logging
