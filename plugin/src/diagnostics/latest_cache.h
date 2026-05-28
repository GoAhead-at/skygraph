#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>

namespace skygraph::diagnostics {

// Shared lock-free cache of the most recent observation from each sampler.
// The stutter flagger reads from here to build a full-context snapshot when
// a frame exceeds the threshold. Updates are wait-free for scalar values;
// the string fields (cell name, in-flight cell) sit behind a tiny mutex
// because std::string isn't lock-free.
//
// Read patterns are dominated by the flagger (one read per frame) and
// writes by the samplers (memory ~1Hz, state ~2Hz, streaming ~2Hz, papyrus
// ~10Hz, cell events sparse). Contention is minimal.
class LatestCache {
public:
    static LatestCache& Get();

    // ---- Memory / VRAM ----------------------------------------------------
    void UpdateMemory(float a_workingSetMb,
                      float a_privateMb,
                      float a_vramUsedMb,
                      float a_vramBudgetMb) noexcept;
    float WorkingSetMb() const noexcept { return _wsMb.load(std::memory_order_acquire); }
    float VramHeadroomMb() const noexcept;

    // ---- Memory pressure --------------------------------------------------
    void UpdatePressure(float a_faultsPerSec,
                        float a_commitChargeMb,
                        float a_commitLimitMb) noexcept;
    float PageFaultsPerSec() const noexcept { return _faults.load(std::memory_order_acquire); }

    // ---- Game state -------------------------------------------------------
    void UpdateState(std::string a_cell,
                     std::uint32_t a_actorsHigh,
                     std::uint32_t a_actorsMidHigh,
                     std::uint32_t a_actorsMidLow,
                     std::uint32_t a_actorsLow,
                     std::uint32_t a_loadedRefs) noexcept;
    std::string CellName() const;
    std::array<std::uint32_t, 4> ActorCounts() const noexcept;
    std::uint32_t LoadedRefs() const noexcept { return _loadedRefs.load(std::memory_order_acquire); }

    // ---- Cell load tracking ----------------------------------------------
    // Called by event_sources on attach start / attach complete to keep the
    // "what cell is currently loading" status up-to-date.
    void NoteCellAttachStart(std::string a_cell);
    void NoteCellAttachComplete(std::string_view a_cell);
    std::string InFlightCellLoad() const;

    // ---- Streaming -------------------------------------------------------
    void UpdateStreaming(std::uint32_t a_queueDepth,
                         std::uint32_t a_inFlight,
                         double a_bytesPerSec) noexcept;
    std::uint32_t StreamingQueueDepth() const noexcept {
        return _streamQueue.load(std::memory_order_acquire);
    }

private:
    LatestCache() = default;

    std::atomic<float> _wsMb{ 0.0f };
    std::atomic<float> _privMb{ 0.0f };
    std::atomic<float> _vramUsedMb{ 0.0f };
    std::atomic<float> _vramBudgetMb{ 0.0f };

    std::atomic<float> _faults{ 0.0f };
    std::atomic<float> _commitChargeMb{ 0.0f };
    std::atomic<float> _commitLimitMb{ 0.0f };

    std::atomic<std::uint32_t> _actorHi{ 0 };
    std::atomic<std::uint32_t> _actorMh{ 0 };
    std::atomic<std::uint32_t> _actorMl{ 0 };
    std::atomic<std::uint32_t> _actorLo{ 0 };
    std::atomic<std::uint32_t> _loadedRefs{ 0 };

    std::atomic<std::uint32_t> _streamQueue{ 0 };
    std::atomic<std::uint32_t> _streamInFlight{ 0 };
    std::atomic<double> _streamBps{ 0.0 };

    mutable std::mutex _stringsMtx;
    std::string _cellName;
    std::string _inFlightCell;
};

}  // namespace skygraph::diagnostics
