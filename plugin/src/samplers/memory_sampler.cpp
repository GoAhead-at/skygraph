#include "samplers/memory_sampler.h"

#include "diagnostics/latest_cache.h"

#include <skygraph/protocol/messages.h>

#include <spdlog/spdlog.h>

#include <Windows.h>
#include <Psapi.h>
#include <dxgi1_4.h>

#include <chrono>
#include <thread>

namespace skygraph::samplers {

namespace {

constexpr double kBytesToMb = 1.0 / (1024.0 * 1024.0);

IDXGIAdapter3* AcquirePrimaryAdapter() {
    IDXGIFactory1* factory = nullptr;
    if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))) return nullptr;

    IDXGIAdapter1* adapter1 = nullptr;
    IDXGIAdapter3* adapter3 = nullptr;
    for (UINT i = 0; factory->EnumAdapters1(i, &adapter1) != DXGI_ERROR_NOT_FOUND; ++i) {
        DXGI_ADAPTER_DESC1 desc{};
        adapter1->GetDesc1(&desc);
        if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) {
            adapter1->Release();
            continue;
        }
        if (SUCCEEDED(adapter1->QueryInterface(IID_PPV_ARGS(&adapter3)))) {
            adapter1->Release();
            break;
        }
        adapter1->Release();
    }
    factory->Release();
    return adapter3;
}

}  // namespace

MemorySampler::MemorySampler(transport::WriterThread& a_writer,
                             double a_memHz,
                             double a_pressureHz)
    : Sampler{ "memory", a_writer }, _memHz{ a_memHz }, _pressureHz{ a_pressureHz } {
    _dxgiAdapter = AcquirePrimaryAdapter();
    if (!_dxgiAdapter) {
        spdlog::warn("memory: failed to acquire DXGI adapter; VRAM fields will be zero");
    }
}

void MemorySampler::Start() {
    bool expected = false;
    if (!_running.compare_exchange_strong(expected, true)) return;
    _thread = std::thread{ [this] { Loop(); } };
}

void MemorySampler::Stop() {
    if (!_running.exchange(false)) return;
    if (_thread.joinable()) _thread.join();
    if (_dxgiAdapter) {
        static_cast<IDXGIAdapter3*>(_dxgiAdapter)->Release();
        _dxgiAdapter = nullptr;
    }
}

void MemorySampler::Loop() {
    using clock = std::chrono::steady_clock;
    using namespace std::chrono;
    using namespace std::chrono_literals;

    const auto memPeriod = duration_cast<clock::duration>(
        duration<double>(_memHz > 0.0 ? 1.0 / _memHz : 1.0));
    const auto pressurePeriod = duration_cast<clock::duration>(
        duration<double>(_pressureHz > 0.0 ? 1.0 / _pressureHz : 1.0));

    auto nextMem = clock::now();
    auto nextPressure = clock::now();

    while (_running.load(std::memory_order_acquire)) {
        auto now = clock::now();
        bool fired = false;
        if (now >= nextMem) {
            EmitMemory();
            nextMem += memPeriod;
            if (nextMem < now) nextMem = now + memPeriod;
            fired = true;
        }
        if (now >= nextPressure) {
            EmitPressure();
            nextPressure += pressurePeriod;
            if (nextPressure < now) nextPressure = now + pressurePeriod;
            fired = true;
        }
        if (!fired) std::this_thread::sleep_for(50ms);
    }
}

void MemorySampler::EmitMemory() {
    PROCESS_MEMORY_COUNTERS_EX pmc{};
    pmc.cb = sizeof(pmc);
    GetProcessMemoryInfo(GetCurrentProcess(),
                         reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&pmc),
                         sizeof(pmc));

    double vramUsedMb = 0.0;
    double vramBudgetMb = 0.0;
    if (_dxgiAdapter) {
        DXGI_QUERY_VIDEO_MEMORY_INFO info{};
        if (SUCCEEDED(static_cast<IDXGIAdapter3*>(_dxgiAdapter)
                          ->QueryVideoMemoryInfo(
                              0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &info))) {
            vramUsedMb = info.CurrentUsage * kBytesToMb;
            vramBudgetMb = info.Budget * kBytesToMb;
        }
    }

    const float ws = static_cast<float>(pmc.WorkingSetSize * kBytesToMb);
    const float priv = static_cast<float>(pmc.PrivateUsage * kBytesToMb);
    const float vu = static_cast<float>(vramUsedMb);
    const float vb = static_cast<float>(vramBudgetMb);
    diagnostics::LatestCache::Get().UpdateMemory(ws, priv, vu, vb);

    nlohmann::json j = {
        { "working_set_mb", ws },
        { "private_mb", priv },
        { "vram_used_mb", vu },
        { "vram_budget_mb", vb },
    };
    Emit(skygraph::protocol::msg::kMemory, std::move(j));
}

void MemorySampler::EmitPressure() {
    PROCESS_MEMORY_COUNTERS pmc{};
    pmc.cb = sizeof(pmc);
    GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc));

    auto now = std::chrono::steady_clock::now();
    const auto currentFaults = static_cast<unsigned long long>(pmc.PageFaultCount);

    double faultsPerSec = 0.0;
    if (_lastPageFaultTime.time_since_epoch().count() != 0) {
        const auto deltaFaults = currentFaults - _lastPageFaultCount;
        const auto deltaSecs = std::chrono::duration<double>(now - _lastPageFaultTime).count();
        if (deltaSecs > 0.0) {
            faultsPerSec = static_cast<double>(deltaFaults) / deltaSecs;
        }
    }
    _lastPageFaultCount = currentFaults;
    _lastPageFaultTime = now;

    MEMORYSTATUSEX mem{};
    mem.dwLength = sizeof(mem);
    GlobalMemoryStatusEx(&mem);

    PERFORMANCE_INFORMATION perf{};
    perf.cb = sizeof(perf);
    double commitChargeMb = 0.0;
    double commitLimitMb = 0.0;
    if (GetPerformanceInfo(&perf, sizeof(perf))) {
        commitChargeMb = perf.CommitTotal * perf.PageSize * kBytesToMb;
        commitLimitMb = perf.CommitLimit * perf.PageSize * kBytesToMb;
    }

    diagnostics::LatestCache::Get().UpdatePressure(
        static_cast<float>(faultsPerSec),
        static_cast<float>(commitChargeMb),
        static_cast<float>(commitLimitMb));

    nlohmann::json j = {
        { "page_faults_per_sec", faultsPerSec },
        { "commit_charge_mb", commitChargeMb },
        { "commit_limit_mb", commitLimitMb },
    };
    Emit(skygraph::protocol::msg::kMemoryPressure, std::move(j));
}

}  // namespace skygraph::samplers
