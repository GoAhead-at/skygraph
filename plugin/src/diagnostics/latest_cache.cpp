#include "diagnostics/latest_cache.h"

namespace skygraph::diagnostics {

LatestCache& LatestCache::Get() {
    static LatestCache c;
    return c;
}

void LatestCache::UpdateMemory(float a_ws, float a_priv, float a_vu, float a_vb) noexcept {
    _wsMb.store(a_ws, std::memory_order_release);
    _privMb.store(a_priv, std::memory_order_release);
    _vramUsedMb.store(a_vu, std::memory_order_release);
    _vramBudgetMb.store(a_vb, std::memory_order_release);
}

float LatestCache::VramHeadroomMb() const noexcept {
    return _vramBudgetMb.load(std::memory_order_acquire)
         - _vramUsedMb.load(std::memory_order_acquire);
}

void LatestCache::UpdatePressure(float a_f, float a_cc, float a_cl) noexcept {
    _faults.store(a_f, std::memory_order_release);
    _commitChargeMb.store(a_cc, std::memory_order_release);
    _commitLimitMb.store(a_cl, std::memory_order_release);
}

void LatestCache::UpdateState(std::string a_cell,
                              std::uint32_t a_h, std::uint32_t a_mh,
                              std::uint32_t a_ml, std::uint32_t a_lo,
                              std::uint32_t a_refs) noexcept {
    _actorHi.store(a_h, std::memory_order_release);
    _actorMh.store(a_mh, std::memory_order_release);
    _actorMl.store(a_ml, std::memory_order_release);
    _actorLo.store(a_lo, std::memory_order_release);
    _loadedRefs.store(a_refs, std::memory_order_release);
    std::lock_guard lk{ _stringsMtx };
    _cellName = std::move(a_cell);
}

std::string LatestCache::CellName() const {
    std::lock_guard lk{ _stringsMtx };
    return _cellName;
}

std::array<std::uint32_t, 4> LatestCache::ActorCounts() const noexcept {
    return {
        _actorHi.load(std::memory_order_acquire),
        _actorMh.load(std::memory_order_acquire),
        _actorMl.load(std::memory_order_acquire),
        _actorLo.load(std::memory_order_acquire),
    };
}

void LatestCache::NoteCellAttachStart(std::string a_cell) {
    std::lock_guard lk{ _stringsMtx };
    _inFlightCell = std::move(a_cell);
}

void LatestCache::NoteCellAttachComplete(std::string_view a_cell) {
    std::lock_guard lk{ _stringsMtx };
    if (_inFlightCell == a_cell || _inFlightCell.empty()) {
        _inFlightCell.clear();
    }
}

std::string LatestCache::InFlightCellLoad() const {
    std::lock_guard lk{ _stringsMtx };
    return _inFlightCell;
}

void LatestCache::UpdateStreaming(std::uint32_t a_q, std::uint32_t a_inf, double a_bps) noexcept {
    _streamQueue.store(a_q, std::memory_order_release);
    _streamInFlight.store(a_inf, std::memory_order_release);
    _streamBps.store(a_bps, std::memory_order_release);
}

}  // namespace skygraph::diagnostics
