#include "samplers/frame_sampler.h"

#include "diagnostics/stutter_flagger.h"
#include "samplers/cpu_breakdown.h"

#include <skygraph/protocol/messages.h>

#include <RE/Skyrim.h>
#include <REL/Relocation.h>
#include <SKSE/SKSE.h>
#include <SKSE/Trampoline.h>

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include <Windows.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <mutex>

namespace skygraph::samplers {

namespace {

// Single-instance state accessed from the hook. The hook runs on the game's
// main thread; the published fields are atomics so the rest of the plugin
// can read them without locks.
struct FrameState {
    transport::WriterThread* writer{ nullptr };

    std::int64_t qpc_freq{ 0 };
    std::int64_t last_present_qpc{ 0 };

    std::atomic<float> last_cpu_ms{ 0.0f };
    std::atomic<float> gpu_ms{ 0.0f };
    std::atomic<float> rolling_p50_ms{ 16.6f };

    // Tiny order-statistic ring for rolling p50 over the last N dt samples.
    // Stored sorted on insertion; capacity matches stutter flagger window.
    static constexpr std::size_t kWindow = 240;
    std::array<float, kWindow> window{};
    std::size_t window_count{ 0 };
    std::size_t window_idx{ 0 };
};

FrameState& State() {
    static FrameState s;
    return s;
}

void UpdateRollingP50(float a_dtMs) {
    auto& s = State();
    if (s.window_count < FrameState::kWindow) {
        s.window[s.window_count++] = a_dtMs;
    } else {
        s.window[s.window_idx] = a_dtMs;
        s.window_idx = (s.window_idx + 1) % FrameState::kWindow;
    }
    // Cheap median: sort a copy. The window is small (240) so this is fine
    // at 60Hz; if it ever shows up in a profile we'll swap for an
    // order-statistic tree.
    std::array<float, FrameState::kWindow> copy{};
    auto n = s.window_count;
    std::copy_n(s.window.begin(), n, copy.begin());
    std::nth_element(copy.begin(), copy.begin() + n / 2, copy.begin() + n);
    s.rolling_p50_ms.store(copy[n / 2], std::memory_order_release);
}

// REL hook on RE::Main::Update. The relocation IDs below cover both SE
// (pre-AE) and AE/post-AE binaries; on Skyrim VR the hook resolves at runtime
// to the equivalent function in REL::Module::IsVR mode (handled by REL::ID).
struct UpdateHook {
    static void thunk(RE::Main* a_main, float a_delta) {
        auto& s = State();

        std::int64_t before = 0;
        if (s.qpc_freq != 0) {
            QueryPerformanceCounter(reinterpret_cast<LARGE_INTEGER*>(&before));
        }

        _original(a_main, a_delta);

        if (s.qpc_freq == 0 || !s.writer) return;

        std::int64_t after = 0;
        QueryPerformanceCounter(reinterpret_cast<LARGE_INTEGER*>(&after));

        const double cpuMs = (after - before) * 1000.0 / s.qpc_freq;
        const double dtMs = s.last_present_qpc == 0
            ? cpuMs
            : (after - s.last_present_qpc) * 1000.0 / s.qpc_freq;
        s.last_present_qpc = after;

        const float dtF = static_cast<float>(dtMs);
        const float cpuF = static_cast<float>(cpuMs);
        s.last_cpu_ms.store(cpuF, std::memory_order_release);

        UpdateRollingP50(dtF);

        const float gpuMs = s.gpu_ms.load(std::memory_order_acquire);
        const double now = std::chrono::duration<double>(
            std::chrono::system_clock::now().time_since_epoch()).count();

        nlohmann::json frame = {
            { skygraph::protocol::kFieldType, skygraph::protocol::msg::kFrame },
            { skygraph::protocol::kFieldTimestamp, now },
            { "dt_ms", dtF },
            { "fps", dtF > 0.0f ? 1000.0f / dtF : 0.0f },
            { "cpu_frame_ms", cpuF },
            { "gpu_frame_ms", gpuMs },
        };
        s.writer->Submit(frame.dump());

        // Drain the cpu_breakdown accumulators that the subsystem hooks have
        // been adding to during this frame, compute residual, and emit a
        // cpu_breakdown record sharing the frame's timestamp.
        const auto snap = CpuBreakdownSampler::ConsumeFrame();
        const float known = snap.papyrus_ms + snap.havok_ms + snap.ai_ms
                          + snap.render_submit_ms + snap.streaming_ms;
        const float other = cpuF > known ? cpuF - known : 0.0f;
        nlohmann::json bd = {
            { skygraph::protocol::kFieldType, skygraph::protocol::msg::kCpuBreakdown },
            { skygraph::protocol::kFieldTimestamp, now },
            { "papyrus_ms", snap.papyrus_ms },
            { "havok_ms", snap.havok_ms },
            { "ai_ms", snap.ai_ms },
            { "render_submit_ms", snap.render_submit_ms },
            { "streaming_ms", snap.streaming_ms },
            { "other_ms", other },
        };
        s.writer->Submit(bd.dump());

        // Hand the just-finalized per-frame numbers to the stutter flagger,
        // which will emit an event.stutter with a full context snapshot if
        // dtF exceeds the configured ratio.
        skygraph::diagnostics::StutterFlagger::OnFrame(
            dtF, cpuF, s.rolling_p50_ms.load(std::memory_order_acquire), snap);
    }

    static inline REL::Relocation<decltype(thunk)> _original;
    static constexpr std::size_t kIndex = 0x11F;  // call offset inside caller
};

}  // namespace

FrameSampler::FrameSampler(transport::WriterThread& a_writer)
    : Sampler{ "frame", a_writer } {
    State().writer = &a_writer;
    LARGE_INTEGER f{};
    QueryPerformanceFrequency(&f);
    State().qpc_freq = f.QuadPart;
}

void FrameSampler::Start() {
    if (_installed.exchange(true)) return;
    if (!TryInstallHook()) {
        spdlog::warn("frame: hook install failed; frame records will not be emitted");
        _installed.store(false);
    } else {
        spdlog::info("frame: Main::Update hook installed");
    }
}

void FrameSampler::Stop() {
    // SKSE Trampoline hooks aren't safely uninstallable; the plugin is
    // expected to live until process exit. Just clear our writer so any
    // late-firing hook becomes a no-op.
    State().writer = nullptr;
}

bool FrameSampler::TryInstallHook() {
    try {
        SKSE::AllocTrampoline(14);
        // Address Library IDs for the inside-Main::Update call site that
        // dispatches per-frame work. SE 35551 / AE 36544 are widely-used
        // anchors; the +0x11F offset places us at the actual call.
        REL::Relocation<std::uintptr_t> target{
            REL::RelocationID{ 35551, 36544 }, UpdateHook::kIndex
        };
        UpdateHook::_original =
            SKSE::GetTrampoline().write_call<5>(target.address(),
                                                &UpdateHook::thunk);
        return true;
    } catch (const std::exception& e) {
        spdlog::error("frame: hook exception: {}", e.what());
        return false;
    } catch (...) {
        spdlog::error("frame: hook unknown exception");
        return false;
    }
}

float FrameSampler::LastCpuFrameMs() noexcept {
    return State().last_cpu_ms.load(std::memory_order_acquire);
}

float FrameSampler::RollingP50Ms() noexcept {
    return State().rolling_p50_ms.load(std::memory_order_acquire);
}

void FrameSampler::PublishGpuFrameMs(float a_ms) noexcept {
    State().gpu_ms.store(a_ms, std::memory_order_release);
}

}  // namespace skygraph::samplers
