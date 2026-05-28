#pragma once

#include <atomic>
#include <chrono>
#include <filesystem>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace skygraph::transport {

// Tees every NDJSON record written to the pipe into a rolling on-disk file.
// Files live under:
//   %USERPROFILE%/Documents/My Games/Skyrim Special Edition/SKSE/<dir>/
// Each file is gzipped NDJSON; we rotate when the wall-clock rolling window
// (default 5 min) is reached. The N most recent rotated files are kept;
// older files are pruned on rotation.
//
// On a `save_session` command, the *current* in-progress window is finalized
// and renamed to a pinned name in addition to the rolling rotation, giving
// the user a permanent copy of "everything I saw up to this moment."
class RollingRecorder {
public:
    RollingRecorder(int a_rollingMinutes, std::string a_directoryName);
    ~RollingRecorder();

    RollingRecorder(const RollingRecorder&) = delete;
    RollingRecorder& operator=(const RollingRecorder&) = delete;

    bool Start();
    void Stop();

    // Hot path: append one already-newline-terminated record. Cheap;
    // serialized via a single mutex (the writer thread is the only producer).
    void Append(std::string_view a_record);

    // Flush + close current file, then resume into a fresh one. Safe to call
    // from a crash handler.
    void RotateNow();

    // Pin the current rolling window to a permanent file. Returns the path
    // on success.
    std::optional<std::filesystem::path> PinCurrent(std::string_view a_name);

    // Absolute path of the directory we write into.
    const std::filesystem::path& Directory() const noexcept { return _dir; }

private:
    int _rollingMinutes;
    std::string _directoryName;

    std::filesystem::path _dir;

    std::mutex _writeMtx;
    void* _gz{ nullptr };  // gzFile from zlib; void* to avoid <zlib.h> in header
    std::filesystem::path _currentPath;
    std::chrono::steady_clock::time_point _windowStart{};

    std::atomic<bool> _running{ false };

    // List of recent rotated files (newest first).
    std::vector<std::filesystem::path> _rotated;
    static constexpr std::size_t kKeepRotations = 3;

    bool OpenNewFile();
    void CloseCurrent();
    void PruneOldFiles();
    std::filesystem::path NewFilePath(std::string_view a_tag = {}) const;
};

}  // namespace skygraph::transport
