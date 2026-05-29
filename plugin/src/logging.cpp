#include "logging.h"

#include <RE/Skyrim.h>
#include <SKSE/SKSE.h>

#include <spdlog/sinks/msvc_sink.h>

#include <chrono>
#include <memory>
#include <system_error>

namespace skygraph::logging {

void Init(std::string_view a_pluginName, spdlog::level::level_enum a_level,
          std::string_view a_subDir) {
    auto logDir = SKSE::log::log_directory();
    if (!logDir) {
        return;
    }

    // Colocate the log with the recorder's session files when a subdir is
    // given (e.g. "<SKSE>/skygraph/skygraph.log"). create_directories is a
    // no-op if it already exists and never throws here (error_code overload).
    if (!a_subDir.empty()) {
        *logDir /= a_subDir;
        std::error_code ec;
        std::filesystem::create_directories(*logDir, ec);
    }

    auto logPath = *logDir / std::filesystem::path(a_pluginName).replace_extension("log");

    auto fileSink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(
        logPath.string(), /*truncate=*/true);
    auto debuggerSink = std::make_shared<spdlog::sinks::msvc_sink_mt>();

    std::vector<spdlog::sink_ptr> sinks{ std::move(fileSink), std::move(debuggerSink) };
    auto logger = std::make_shared<spdlog::logger>(
        std::string{ a_pluginName }, sinks.begin(), sinks.end());

    logger->set_level(a_level);
    // Flush on every record. Skyrim keeps the process alive for hours and
    // exits hard (or crashes) far more often than it shuts down cleanly, so a
    // higher flush threshold leaves the file empty/stale on disk exactly when
    // someone is trying to read it mid-session. This is a low-volume diagnostic
    // log, so per-record flushing costs nothing meaningful.
    logger->flush_on(a_level);
    logger->set_pattern("[%H:%M:%S.%e] [%^%l%$] [%t] %v");

    spdlog::set_default_logger(std::move(logger));
    // Belt-and-suspenders: also drain the buffer on a timer so anything below
    // the flush threshold still lands within a second.
    spdlog::flush_every(std::chrono::seconds(1));
}

}  // namespace skygraph::logging
