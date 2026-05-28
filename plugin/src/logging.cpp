#include "logging.h"

#include <RE/Skyrim.h>
#include <SKSE/SKSE.h>

#include <spdlog/sinks/msvc_sink.h>

#include <memory>

namespace skygraph::logging {

void Init(std::string_view a_pluginName, spdlog::level::level_enum a_level) {
    auto logDir = SKSE::log::log_directory();
    if (!logDir) {
        return;
    }

    auto logPath = *logDir / std::filesystem::path(a_pluginName).replace_extension("log");

    auto fileSink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(
        logPath.string(), /*truncate=*/true);
    auto debuggerSink = std::make_shared<spdlog::sinks::msvc_sink_mt>();

    std::vector<spdlog::sink_ptr> sinks{ std::move(fileSink), std::move(debuggerSink) };
    auto logger = std::make_shared<spdlog::logger>(
        std::string{ a_pluginName }, sinks.begin(), sinks.end());

    logger->set_level(a_level);
    logger->flush_on(spdlog::level::warn);
    logger->set_pattern("[%H:%M:%S.%e] [%^%l%$] [%t] %v");

    spdlog::set_default_logger(std::move(logger));
}

}  // namespace skygraph::logging
