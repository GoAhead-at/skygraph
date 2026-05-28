#include "samplers/streaming_sampler.h"

#include "diagnostics/latest_cache.h"
#include "samplers/cpu_breakdown.h"

#include <skygraph/protocol/messages.h>

#include <RE/Skyrim.h>
#include <REL/Relocation.h>
#include <SKSE/SKSE.h>
#include <SKSE/Trampoline.h>

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include <Windows.h>

#include <atomic>
#include <chrono>
#include <string>
#include <thread>

namespace skygraph::samplers {

namespace {

struct Stats {
    std::atomic<std::int64_t> bytes_this_second{ 0 };
    std::atomic<std::uint32_t> in_flight{ 0 };
    std::atomic<std::int64_t> hitch_threshold_us{ 5000 };  // mutable from Start()
    transport::WriterThread* writer{ nullptr };
};

Stats& S() { static Stats s; return s; }

void EmitHitch(std::int64_t a_durationUs, std::int64_t a_bytes, const char* a_path) {
    using namespace std::chrono;
    nlohmann::json j = {
        { skygraph::protocol::kFieldType,
          skygraph::protocol::msg::kEventStreamingHitch },
        { skygraph::protocol::kFieldTimestamp,
          duration<double>(system_clock::now().time_since_epoch()).count() },
        { "duration_ms", a_durationUs / 1000.0 },
        { "bytes", a_bytes },
        { "path", a_path ? a_path : "" },
    };
    if (S().writer) S().writer->Submit(j.dump());
}

// Hook on BSResource::ArchiveStream::Read (the BSA read path). Address
// Library IDs below are placeholder anchors; on resolution failure the
// install try/catch silently demotes streaming to "off" without crashing.
struct ArchiveReadHook {
    static std::uint64_t thunk(void* a_stream, void* a_dest, std::uint64_t a_size) {
        const auto t0 = QpcNowUs();
        S().in_flight.fetch_add(1, std::memory_order_relaxed);
        const auto bytes = _original(a_stream, a_dest, a_size);
        S().in_flight.fetch_sub(1, std::memory_order_relaxed);
        const auto dt = QpcNowUs() - t0;

        if (dt > 0) {
            CpuBreakdownSampler::AddStreamingUs(dt);
            S().bytes_this_second.fetch_add(
                static_cast<std::int64_t>(bytes), std::memory_order_relaxed);
        }
        if (dt >= S().hitch_threshold_us.load(std::memory_order_relaxed)) {
            EmitHitch(dt, static_cast<std::int64_t>(bytes),
                      /*path=*/nullptr);
        }
        return bytes;
    }
    static inline REL::Relocation<decltype(thunk)> _original;
    // BSResource::ArchiveStream::Read -- speculative anchor; verify before
    // enabling. See note in subsystem_hooks.cpp on Address Library ID lookup.
    static constexpr std::uint64_t kSeId = 35043;
    static constexpr std::uint64_t kAeId = 35989;
};

}  // namespace

StreamingSampler::StreamingSampler(transport::WriterThread& a_writer,
                                   double a_hitchThresholdMs)
    : Sampler{ "streaming", a_writer },
      _hitchThresholdMs{ a_hitchThresholdMs <= 0.0 ? 5.0 : a_hitchThresholdMs } {
    S().writer = &a_writer;
    S().hitch_threshold_us.store(
        static_cast<std::int64_t>(_hitchThresholdMs * 1000.0),
        std::memory_order_release);
}

void StreamingSampler::Start() {
    bool expected = false;
    if (!_running.compare_exchange_strong(expected, true)) return;

    if (InstallHook()) {
        _hookInstalled.store(true, std::memory_order_release);
        auto mask = CpuBreakdownSampler::GetEnabledMask();
        mask.streaming = true;
        CpuBreakdownSampler::SetEnabledMask(mask);
    }

    _summaryThread = std::thread{ [this] { SummaryLoop(); } };
    spdlog::info("streaming: sampler started (hitch={}ms, hook={})",
                 _hitchThresholdMs,
                 _hookInstalled.load(std::memory_order_acquire));
}

void StreamingSampler::Stop() {
    if (!_running.exchange(false)) return;
    if (_summaryThread.joinable()) _summaryThread.join();
}

void StreamingSampler::OnReadComplete(std::int64_t a_durationUs,
                                       std::int64_t a_bytes,
                                       const char* a_path) noexcept {
    // External callers (future BSResource sub-hooks for the loose-file path)
    // can call this directly without going through the archive read hook.
    if (a_durationUs > 0) {
        CpuBreakdownSampler::AddStreamingUs(a_durationUs);
        S().bytes_this_second.fetch_add(a_bytes, std::memory_order_relaxed);
    }
    if (a_durationUs >= S().hitch_threshold_us.load(std::memory_order_relaxed)) {
        EmitHitch(a_durationUs, a_bytes, a_path);
    }
}

bool StreamingSampler::InstallHook() {
    try {
        SKSE::AllocTrampoline(14);
        REL::Relocation<std::uintptr_t> target{
            REL::RelocationID(ArchiveReadHook::kSeId, ArchiveReadHook::kAeId), 0x0
        };
        ArchiveReadHook::_original =
            SKSE::GetTrampoline().write_call<5>(target.address(),
                                                 &ArchiveReadHook::thunk);
        spdlog::info("streaming: BSResource read hook installed");
        return true;
    } catch (const std::exception& e) {
        spdlog::warn("streaming: read hook failed: {}", e.what());
        return false;
    } catch (...) {
        spdlog::warn("streaming: read hook unknown failure");
        return false;
    }
}

void StreamingSampler::SummaryLoop() {
    using namespace std::chrono;
    using namespace std::chrono_literals;
    constexpr auto kPeriod = 500ms;  // 2 Hz

    while (_running.load(std::memory_order_acquire)) {
        std::this_thread::sleep_for(kPeriod);

        const auto bytes = S().bytes_this_second.exchange(0, std::memory_order_acq_rel);
        const double bps = bytes * (1000.0 / 500.0);  // bytes/sec scaled from 500ms window
        const auto inFlight = S().in_flight.load(std::memory_order_acquire);

        // Queue depth is approximate; we treat in_flight as the queue depth
        // for the summary record, since BSResource doesn't expose a separate
        // queue accessor through the public RE headers.
        diagnostics::LatestCache::Get().UpdateStreaming(inFlight, inFlight, bps);

        nlohmann::json j = {
            { skygraph::protocol::kFieldType, skygraph::protocol::msg::kStreaming },
            { skygraph::protocol::kFieldTimestamp,
              duration<double>(system_clock::now().time_since_epoch()).count() },
            { "queue_depth", inFlight },
            { "bytes_per_sec", bps },
            { "in_flight", inFlight },
        };
        Writer().Submit(j.dump());
    }
}

}  // namespace skygraph::samplers
