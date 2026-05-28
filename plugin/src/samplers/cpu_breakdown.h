#pragma once

#include "samplers/sampler.h"

#include <atomic>
#include <cstdint>

namespace skygraph::samplers {

// Per-subsystem CPU time accumulator. Each subsystem hook (Havok world step,
// ProcessLists::Update, render submit, BSResource streaming, the Papyrus VM
// stack runner) adds microseconds to its bucket via the static Add* helpers.
//
// At end-of-frame the FrameSampler calls ConsumeFrame() to drain the buckets
// into a single snapshot, computes `other_ms` as the residual against the
// measured cpu_frame_ms, and emits a `cpu_breakdown` record alongside the
// `frame` record so both share a timestamp.
//
// Each individual hook is feature-flagged in skygraph.json. A hook that fails
// to install simply doesn't add to its bucket; that subsystem's contribution
// rolls silently into `other_ms` so the breakdown stays consistent.
class CpuBreakdownSampler : public Sampler {
public:
    struct Snapshot {
        float papyrus_ms{ 0.0f };
        float havok_ms{ 0.0f };
        float ai_ms{ 0.0f };
        float render_submit_ms{ 0.0f };
        float streaming_ms{ 0.0f };
    };

    explicit CpuBreakdownSampler(transport::WriterThread& a_writer);

    void Start() override;
    void Stop() override;

    // Sub-hook entry points. Safe to call from any thread; the hot path is a
    // single fetch_add on an atomic counter.
    static void AddPapyrusUs(std::int64_t a_us) noexcept;
    static void AddHavokUs(std::int64_t a_us) noexcept;
    static void AddAiUs(std::int64_t a_us) noexcept;
    static void AddRenderSubmitUs(std::int64_t a_us) noexcept;
    static void AddStreamingUs(std::int64_t a_us) noexcept;

    // Called by the FrameSampler at the end of every frame. Atomically drains
    // each accumulator and returns the converted (us -> ms) snapshot.
    static Snapshot ConsumeFrame() noexcept;

    // Records which subsystems are actually instrumented this session so the
    // viewer can display "subsystem X disabled" hints. Set during Start().
    struct EnabledMask {
        bool havok{ false };
        bool ai{ false };
        bool render_submit{ false };
        bool streaming{ false };
        bool papyrus{ false };
    };
    static EnabledMask GetEnabledMask() noexcept;
    static void SetEnabledMask(EnabledMask a_m) noexcept;
};

// Cross-platform monotonic microseconds via QueryPerformanceCounter. Inline
// so sub-hooks can use it without pulling more headers.
std::int64_t QpcNowUs() noexcept;

// Convenience RAII bracket: scopes a QPC delta and adds it to the chosen
// bucket on destruction. Use with one of the CpuBreakdownSampler::Add* refs.
template <auto Adder>
class CpuBracket {
public:
    CpuBracket() noexcept : _start{ QpcNowUs() } {}
    ~CpuBracket() noexcept {
        Adder(QpcNowUs() - _start);
    }

    CpuBracket(const CpuBracket&) = delete;
    CpuBracket& operator=(const CpuBracket&) = delete;

private:
    std::int64_t _start;
};

}  // namespace skygraph::samplers
