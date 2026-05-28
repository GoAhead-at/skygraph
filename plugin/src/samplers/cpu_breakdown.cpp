#include "samplers/cpu_breakdown.h"

#include <spdlog/spdlog.h>

#include <Windows.h>

#include <atomic>

namespace skygraph::samplers {

namespace {

// One accumulator per subsystem, in microseconds. Drained to zero by
// ConsumeFrame() at end-of-frame.
struct Accumulators {
    std::atomic<std::int64_t> papyrus_us{ 0 };
    std::atomic<std::int64_t> havok_us{ 0 };
    std::atomic<std::int64_t> ai_us{ 0 };
    std::atomic<std::int64_t> render_submit_us{ 0 };
    std::atomic<std::int64_t> streaming_us{ 0 };
};

Accumulators& Acc() {
    static Accumulators a;
    return a;
}

struct MaskHolder {
    std::atomic<std::uint32_t> bits{ 0 };  // packed: bit0=havok, 1=ai, 2=rs, 3=stream, 4=papy
};
MaskHolder& Mask() { static MaskHolder m; return m; }

std::int64_t QpcFreq() {
    static const std::int64_t freq = []() {
        LARGE_INTEGER f{};
        QueryPerformanceFrequency(&f);
        return f.QuadPart;
    }();
    return freq;
}

}  // namespace

std::int64_t QpcNowUs() noexcept {
    LARGE_INTEGER c{};
    QueryPerformanceCounter(&c);
    // ticks * 1e6 / freq, guarding against overflow on long sessions.
    const auto freq = QpcFreq();
    if (freq <= 0) return 0;
    // (c / freq) * 1e6 + (c % freq) * 1e6 / freq -- keeps precision over years.
    return (c.QuadPart / freq) * 1'000'000
           + (c.QuadPart % freq) * 1'000'000 / freq;
}

CpuBreakdownSampler::CpuBreakdownSampler(transport::WriterThread& a_writer)
    : Sampler{ "cpu_breakdown", a_writer } {}

void CpuBreakdownSampler::Start() {
    // The sampler itself owns no thread -- it's driven entirely by the
    // sub-hooks (each installed by its respective sampler) plus the frame
    // sampler's ConsumeFrame() at end-of-frame. So Start/Stop are just
    // lifecycle markers.
    spdlog::info("cpu_breakdown: accumulator active (sub-hooks register "
                 "independently)");
}

void CpuBreakdownSampler::Stop() {}

void CpuBreakdownSampler::AddPapyrusUs(std::int64_t a_us) noexcept {
    if (a_us > 0) Acc().papyrus_us.fetch_add(a_us, std::memory_order_relaxed);
}
void CpuBreakdownSampler::AddHavokUs(std::int64_t a_us) noexcept {
    if (a_us > 0) Acc().havok_us.fetch_add(a_us, std::memory_order_relaxed);
}
void CpuBreakdownSampler::AddAiUs(std::int64_t a_us) noexcept {
    if (a_us > 0) Acc().ai_us.fetch_add(a_us, std::memory_order_relaxed);
}
void CpuBreakdownSampler::AddRenderSubmitUs(std::int64_t a_us) noexcept {
    if (a_us > 0) Acc().render_submit_us.fetch_add(a_us, std::memory_order_relaxed);
}
void CpuBreakdownSampler::AddStreamingUs(std::int64_t a_us) noexcept {
    if (a_us > 0) Acc().streaming_us.fetch_add(a_us, std::memory_order_relaxed);
}

CpuBreakdownSampler::Snapshot CpuBreakdownSampler::ConsumeFrame() noexcept {
    auto& a = Acc();
    const auto p = a.papyrus_us.exchange(0, std::memory_order_acq_rel);
    const auto h = a.havok_us.exchange(0, std::memory_order_acq_rel);
    const auto i = a.ai_us.exchange(0, std::memory_order_acq_rel);
    const auto r = a.render_submit_us.exchange(0, std::memory_order_acq_rel);
    const auto s = a.streaming_us.exchange(0, std::memory_order_acq_rel);
    constexpr float kUsToMs = 1.0f / 1000.0f;
    return Snapshot{
        p * kUsToMs, h * kUsToMs, i * kUsToMs, r * kUsToMs, s * kUsToMs
    };
}

CpuBreakdownSampler::EnabledMask CpuBreakdownSampler::GetEnabledMask() noexcept {
    auto b = Mask().bits.load(std::memory_order_acquire);
    return EnabledMask{
        .havok = (b & 1u) != 0,
        .ai = (b & 2u) != 0,
        .render_submit = (b & 4u) != 0,
        .streaming = (b & 8u) != 0,
        .papyrus = (b & 16u) != 0,
    };
}

void CpuBreakdownSampler::SetEnabledMask(EnabledMask a_m) noexcept {
    std::uint32_t b = 0;
    if (a_m.havok) b |= 1u;
    if (a_m.ai) b |= 2u;
    if (a_m.render_submit) b |= 4u;
    if (a_m.streaming) b |= 8u;
    if (a_m.papyrus) b |= 16u;
    Mask().bits.store(b, std::memory_order_release);
}

}  // namespace skygraph::samplers
