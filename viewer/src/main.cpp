#include "app.h"

#include <spdlog/spdlog.h>
#include <spdlog/sinks/basic_file_sink.h>

#include <Windows.h>
#include <shellapi.h>

#include <filesystem>
#include <string>

namespace {

std::filesystem::path ExeDirectory() {
    wchar_t buf[MAX_PATH];
    GetModuleFileNameW(nullptr, buf, MAX_PATH);
    return std::filesystem::path{ buf }.parent_path();
}

void InitLogging() {
    try {
        auto logPath = ExeDirectory() / "skygraph-viewer.log";
        auto sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(
            logPath.string(), true);
        auto logger = std::make_shared<spdlog::logger>("viewer", std::move(sink));
        logger->set_level(spdlog::level::info);
        logger->flush_on(spdlog::level::warn);
        logger->set_pattern("[%H:%M:%S.%e] [%^%l%$] %v");
        spdlog::set_default_logger(std::move(logger));
    } catch (...) {
        // Best-effort; we don't fail launch if the log file can't be opened.
    }
}

// Parse the lpCmdLine string. Supports a single optional positional argument:
// a path to a .ndjson or .ndjson.gz file for replay mode.
std::string ParseReplayPath(LPSTR a_cmdLine) {
    if (!a_cmdLine) return {};
    std::string s{ a_cmdLine };
    // Trim quotes.
    while (!s.empty() && (s.front() == '"' || s.front() == ' ')) s.erase(s.begin());
    while (!s.empty() && (s.back() == '"' || s.back() == ' ')) s.pop_back();
    return s;
}

}  // namespace

int APIENTRY wWinMain(HINSTANCE, HINSTANCE, LPWSTR, int) {
    InitLogging();
    spdlog::info("skygraph viewer starting");

    int argc = 0;
    auto** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    std::string replayPath;
    if (argc >= 2 && argv) {
        char buf[MAX_PATH * 4]{};
        WideCharToMultiByte(CP_UTF8, 0, argv[1], -1,
                            buf, sizeof(buf), nullptr, nullptr);
        replayPath = buf;
    }
    if (argv) LocalFree(argv);

    skygraph::viewer::App::Options opts;
    opts.replay_path = replayPath;
    skygraph::viewer::App app{ std::move(opts) };
    return app.Run();
}
