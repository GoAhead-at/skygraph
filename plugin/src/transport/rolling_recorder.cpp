#include "transport/rolling_recorder.h"

#include <SKSE/SKSE.h>

#include <spdlog/spdlog.h>

#include <fmt/format.h>

#include <zlib.h>

#include <Windows.h>

#include <algorithm>
#include <ctime>

namespace skygraph::transport {

namespace {

std::string TimestampForFilename() {
    auto now = std::time(nullptr);
    std::tm tm{};
    localtime_s(&tm, &now);
    return fmt::format("{:04d}{:02d}{:02d}-{:02d}{:02d}{:02d}",
                       tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
                       tm.tm_hour, tm.tm_min, tm.tm_sec);
}

std::filesystem::path SkseLogParent() {
    // SKSE log dir = "<docs>/My Games/.../SKSE/" -- great parent for our files.
    auto p = SKSE::log::log_directory();
    if (p) return *p;
    // Fallback: %TEMP%
    wchar_t buf[MAX_PATH];
    DWORD n = GetTempPathW(MAX_PATH, buf);
    return std::filesystem::path{ std::wstring{ buf, n } };
}

}  // namespace

RollingRecorder::RollingRecorder(int a_rollingMinutes, std::string a_dir)
    : _rollingMinutes{ a_rollingMinutes <= 0 ? 5 : a_rollingMinutes },
      _directoryName{ std::move(a_dir) } {}

RollingRecorder::~RollingRecorder() { Stop(); }

bool RollingRecorder::Start() {
    bool expected = false;
    if (!_running.compare_exchange_strong(expected, true)) return false;

    _dir = SkseLogParent() / _directoryName;
    std::error_code ec;
    std::filesystem::create_directories(_dir, ec);
    if (ec) {
        spdlog::warn("recorder: failed to create '{}': {}",
                     _dir.string(), ec.message());
        _running.store(false, std::memory_order_release);
        return false;
    }

    if (!OpenNewFile()) {
        _running.store(false, std::memory_order_release);
        return false;
    }
    spdlog::info("recorder: started, writing to '{}'", _dir.string());
    return true;
}

void RollingRecorder::Stop() {
    if (!_running.exchange(false)) return;
    std::lock_guard lk{ _writeMtx };
    CloseCurrent();
}

void RollingRecorder::Append(std::string_view a_record) {
    if (!_running.load(std::memory_order_acquire)) return;
    std::lock_guard lk{ _writeMtx };
    if (!_gz) return;

    // Rotate by wall-clock window.
    const auto now = std::chrono::steady_clock::now();
    if (now - _windowStart >= std::chrono::minutes{ _rollingMinutes }) {
        CloseCurrent();
        if (!OpenNewFile()) return;
    }

    auto gz = static_cast<gzFile>(_gz);
    if (gzwrite(gz, a_record.data(), static_cast<unsigned>(a_record.size())) == 0) {
        spdlog::warn("recorder: gzwrite failed; closing file");
        CloseCurrent();
    }
}

void RollingRecorder::RotateNow() {
    std::lock_guard lk{ _writeMtx };
    if (_gz) {
        CloseCurrent();
        OpenNewFile();
    }
}

std::optional<std::filesystem::path>
RollingRecorder::PinCurrent(std::string_view a_name) {
    std::lock_guard lk{ _writeMtx };
    if (!_gz) return std::nullopt;

    // Sanitize name: keep alnum, dash, underscore.
    std::string clean;
    clean.reserve(a_name.size());
    for (char c : a_name) {
        if (std::isalnum(static_cast<unsigned char>(c))
            || c == '-' || c == '_') clean.push_back(c);
    }
    if (clean.empty()) clean = "pinned";

    const auto pinPath = _dir / fmt::format("pinned-{}-{}.ndjson.gz",
                                            clean, TimestampForFilename());

    // Finalize current file by closing it, then duplicate its bytes to pinPath
    // and reopen a fresh rolling file so we don't drop subsequent records.
    auto current = _currentPath;
    CloseCurrent();

    std::error_code ec;
    std::filesystem::copy_file(current, pinPath,
        std::filesystem::copy_options::overwrite_existing, ec);
    if (ec) {
        spdlog::warn("recorder: copy to pin '{}' failed: {}",
                     pinPath.string(), ec.message());
        OpenNewFile();
        return std::nullopt;
    }
    spdlog::info("recorder: pinned session to '{}'", pinPath.string());
    OpenNewFile();
    return pinPath;
}

bool RollingRecorder::OpenNewFile() {
    _currentPath = NewFilePath();
    auto gz = gzopen(_currentPath.string().c_str(), "wb");
    if (!gz) {
        spdlog::warn("recorder: gzopen '{}' failed", _currentPath.string());
        return false;
    }
    _gz = gz;
    _windowStart = std::chrono::steady_clock::now();
    return true;
}

void RollingRecorder::CloseCurrent() {
    if (_gz) {
        gzclose(static_cast<gzFile>(_gz));
        _gz = nullptr;
        _rotated.insert(_rotated.begin(), _currentPath);
        PruneOldFiles();
    }
}

void RollingRecorder::PruneOldFiles() {
    while (_rotated.size() > kKeepRotations) {
        std::error_code ec;
        std::filesystem::remove(_rotated.back(), ec);
        _rotated.pop_back();
    }
}

std::filesystem::path RollingRecorder::NewFilePath(std::string_view a_tag) const {
    std::string name = "session-" + TimestampForFilename();
    if (!a_tag.empty()) { name += '-'; name.append(a_tag); }
    name += ".ndjson.gz";
    return _dir / name;
}

}  // namespace skygraph::transport
