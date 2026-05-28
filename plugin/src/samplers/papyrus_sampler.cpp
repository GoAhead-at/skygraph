#include "samplers/papyrus_sampler.h"

#include "samplers/cpu_breakdown.h"
#include "samplers/papyrus_attribution.h"

#include <skygraph/protocol/messages.h>

#include <RE/Skyrim.h>
#include <REL/Relocation.h>
#include <SKSE/SKSE.h>
#include <SKSE/Trampoline.h>

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include <Windows.h>

#include <chrono>
#include <string>
#include <thread>

namespace skygraph::samplers {

namespace {

// Resolve a tasklet's owning script name. Defensive against missing pieces
// of the chain; returns empty if we can't.
std::string ScriptNameFromStack(RE::BSScript::Stack* a_stack) {
    if (!a_stack) return {};
    auto* frame = a_stack->top;
    if (!frame) return {};
    if (auto& owningObject = frame->owningObjectType) {
        return owningObject->name.c_str();
    }
    return {};
}

// VM stack run hook: invoked once per stack execution slice. We bracket with
// QPC, attribute the time to the script name, and feed cpu_breakdown.
//
// The exact function being hooked is the per-stack dispatch entry inside
// BSScript::Internal::VirtualMachine. Address Library IDs below resolve to
// the same call site across SE / AE; if a future runtime rearranges, the
// install try/catch demotes us to "no per-script attribution" without
// crashing.
struct StackRunHook {
    static std::uint32_t thunk(RE::BSScript::Internal::VirtualMachine* a_vm,
                               RE::BSTSmartPointer<RE::BSScript::Stack>& a_stack,
                               RE::BSScript::ErrorLogger& a_logger,
                               RE::BSScript::Internal::IFuncCallQuery& a_query) {
        const auto t0 = QpcNowUs();
        const auto ret = _original(a_vm, a_stack, a_logger, a_query);
        const auto dt = QpcNowUs() - t0;
        if (dt > 0) {
            CpuBreakdownSampler::AddPapyrusUs(dt);
            const auto name = ScriptNameFromStack(a_stack.get());
            if (!name.empty()) {
                PapyrusAttribution::Add(name, static_cast<std::uint64_t>(dt));
            }
        }
        return ret;
    }
    static inline REL::Relocation<decltype(thunk)> _original;
    // VM::ExecuteStack -- speculative anchor; verify before enabling.
    // See note in subsystem_hooks.cpp on Address Library ID lookup.
    static constexpr std::uint64_t kSeId = 98520;
    static constexpr std::uint64_t kAeId = 105200;
};

void EmitVmCounters(transport::WriterThread& a_writer) {
    auto* vm = RE::BSScript::Internal::VirtualMachine::GetSingleton();
    if (!vm) return;

    // VM internal field names per CommonLibSSE-NG v3.7.0 layout:
    //   active    = allRunningStacks
    //   suspended = suspendQueue1 + suspendQueue2 (+ overflow arrays)
    //   latent    = waitingLatentReturns
    std::uint32_t active = 0;
    std::uint32_t suspended = 0;
    std::uint32_t latent = 0;
    try {
        active = static_cast<std::uint32_t>(vm->allRunningStacks.size());
        suspended = static_cast<std::uint32_t>(
            vm->overflowSuspendArray1.size() + vm->overflowSuspendArray2.size());
        latent = static_cast<std::uint32_t>(vm->waitingLatentReturns.size());
    } catch (...) {
        // Some runtime layouts may not expose every field; tolerate.
    }

    using namespace std::chrono;
    nlohmann::json j = {
        { skygraph::protocol::kFieldType, skygraph::protocol::msg::kPapyrusSnapshot },
        { skygraph::protocol::kFieldTimestamp,
          duration<double>(system_clock::now().time_since_epoch()).count() },
        { "active", active },
        { "suspended", suspended },
        { "latent", latent },
    };
    a_writer.Submit(j.dump());
}

void EmitTopScripts(transport::WriterThread& a_writer, int a_topN, double a_periodSec) {
    // Decay factor chosen so old data falls off over ~6 windows (~0.6s at 10Hz).
    constexpr double kDecay = 0.55;
    auto top = PapyrusAttribution::SnapshotTopAndDecay(
        static_cast<std::size_t>(a_topN), kDecay, a_periodSec);

    using namespace std::chrono;
    nlohmann::json scripts = nlohmann::json::array();
    for (const auto& e : top) {
        const double cps = a_periodSec > 0.0
            ? static_cast<double>(e.calls_in_window) / a_periodSec
            : 0.0;
        // pct_frame is rough: assume 60fps target; refined in stutter_flagger
        // phase by joining against actual frame time.
        const double pctFrame = (e.total_us / 1000.0) / (16.667 * (a_periodSec / 0.0167));
        scripts.push_back({
            { "name", e.name },
            { "us_window", e.total_us },
            { "cps", cps },
            { "pct_frame", pctFrame },
        });
    }

    nlohmann::json j = {
        { skygraph::protocol::kFieldType, skygraph::protocol::msg::kPapyrusTop },
        { skygraph::protocol::kFieldTimestamp,
          duration<double>(system_clock::now().time_since_epoch()).count() },
        { "scripts", std::move(scripts) },
    };
    a_writer.Submit(j.dump());
}

}  // namespace

PapyrusSampler::PapyrusSampler(transport::WriterThread& a_writer,
                               double a_snapshotHz,
                               int a_topN,
                               bool a_installVmHook)
    : Sampler{ "papyrus", a_writer },
      _snapshotHz{ a_snapshotHz <= 0.0 ? 10.0 : a_snapshotHz },
      _topN{ a_topN <= 0 ? 16 : a_topN },
      _installVmHook{ a_installVmHook } {}

void PapyrusSampler::Start() {
    bool expected = false;
    if (!_running.compare_exchange_strong(expected, true)) return;

    if (_installVmHook) {
        if (InstallHook()) {
            _hookInstalled.store(true, std::memory_order_release);
            auto mask = CpuBreakdownSampler::GetEnabledMask();
            mask.papyrus = true;
            CpuBreakdownSampler::SetEnabledMask(mask);
        }
    } else {
        spdlog::info("papyrus: vm_hook disabled in config -- per-script CPU "
                     "attribution off, snapshots still running");
    }

    _thread = std::thread{ [this] { SnapshotLoop(); } };
    spdlog::info("papyrus: sampler started (hz={}, top_n={}, hook={})",
                 _snapshotHz, _topN,
                 _hookInstalled.load(std::memory_order_acquire));
}

void PapyrusSampler::Stop() {
    if (!_running.exchange(false)) return;
    if (_thread.joinable()) _thread.join();
    PapyrusAttribution::Reset();
}

bool PapyrusSampler::InstallHook() {
    try {
        SKSE::AllocTrampoline(14);
        REL::Relocation<std::uintptr_t> target{
            REL::RelocationID(StackRunHook::kSeId, StackRunHook::kAeId), 0x0
        };
        StackRunHook::_original =
            SKSE::GetTrampoline().write_call<5>(target.address(),
                                                 &StackRunHook::thunk);
        spdlog::info("papyrus: VM stack hook installed");
        return true;
    } catch (const std::exception& e) {
        spdlog::warn("papyrus: VM stack hook failed: {}", e.what());
        return false;
    } catch (...) {
        spdlog::warn("papyrus: VM stack hook unknown failure");
        return false;
    }
}

void PapyrusSampler::SnapshotLoop() {
    using namespace std::chrono;
    using namespace std::chrono_literals;

    const auto period = duration_cast<steady_clock::duration>(
        duration<double>(1.0 / _snapshotHz));
    auto next = steady_clock::now();
    const double periodSec = 1.0 / _snapshotHz;

    while (_running.load(std::memory_order_acquire)) {
        const auto now = steady_clock::now();
        if (now >= next) {
            EmitVmCounters(Writer());
            if (_hookInstalled.load(std::memory_order_acquire)) {
                EmitTopScripts(Writer(), _topN, periodSec);
            }
            next += period;
            if (next < now) next = now + period;
        } else {
            std::this_thread::sleep_for(5ms);
        }
    }
}

void PapyrusSampler::EmitSnapshot() { EmitVmCounters(Writer()); }
void PapyrusSampler::EmitTop() {
    EmitTopScripts(Writer(), _topN, 1.0 / _snapshotHz);
}

}  // namespace skygraph::samplers
