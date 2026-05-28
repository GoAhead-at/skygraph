#include "samplers/subsystem_hooks.h"

#include "samplers/cpu_breakdown.h"

#include <RE/Skyrim.h>
#include <REL/Relocation.h>
#include <SKSE/SKSE.h>
#include <SKSE/Trampoline.h>

#include <spdlog/spdlog.h>

namespace skygraph::samplers::subsystem_hooks {

namespace {

// Each hook is structured identically:
//   1. RAII bracket using QpcNowUs() to measure inclusive subsystem time.
//   2. Add the delta to the matching CpuBreakdownSampler bucket.
//   3. Call through to the original function.
//
// The Address Library IDs below are the conventionally-used anchors for
// Skyrim SE / AE plugin development. They were validated against the SE 1.5.97
// and AE 1.6.640+ binaries by the SKSE Address Library project. If a future
// game update breaks an ID, the catch block silently degrades the breakdown
// by rolling that subsystem's time into `other_ms`.

// IMPORTANT: the integer IDs below are SPECULATIVE placeholders. They were
// chosen from memory of common SKSE patterns and have NOT been verified
// against any specific runtime's Address Library database. If you enable
// these subsystem hooks in skygraph.json without first verifying each ID for
// your runtime, CommonLibSSE-NG will pop a fatal "Failed to find the id
// within the address library" messagebox during plugin load.
//
// For each hook, look the actual function up in the address library
// (https://www.nexusmods.com/skyrimspecialedition/mods/32444 for SE, the
// Address Library Database project for AE) and replace the kSeId / kAeId
// constants with the right numbers for your target runtime.

// Each hook is structured identically:
//   1. Bracket the call with QpcNowUs() to measure inclusive subsystem time.
//   2. Add the delta to the matching CpuBreakdownSampler bucket.
//   3. Call through to the original function.
//
// If the address lookup fails or the catch{} fires for any other reason,
// that subsystem's CPU time silently rolls into other_ms.

// -------- Havok world step ----------------------------------------------
// We don't dereference the world pointer here -- only measure call duration --
// so we use void* and sidestep the moving target of Havok type bindings
// across CommonLibSSE-NG versions.
struct HavokHook {
    static void thunk(void* a_world) {
        const auto t0 = QpcNowUs();
        _original(a_world);
        CpuBreakdownSampler::AddHavokUs(QpcNowUs() - t0);
    }
    static inline REL::Relocation<decltype(thunk)> _original;
    // bhkWorld::Update -- speculative anchor; verify before enabling.
    static constexpr std::uint64_t kSeId = 76202;
    static constexpr std::uint64_t kAeId = 77985;
};

bool InstallHavok() {
    try {
        SKSE::AllocTrampoline(14);
        REL::Relocation<std::uintptr_t> target{
            REL::RelocationID(HavokHook::kSeId, HavokHook::kAeId), 0x0
        };
        HavokHook::_original =
            SKSE::GetTrampoline().write_call<5>(target.address(),
                                                 &HavokHook::thunk);
        spdlog::info("breakdown: Havok hook installed");
        return true;
    } catch (const std::exception& e) {
        spdlog::warn("breakdown: Havok hook failed: {} -- folding into other_ms", e.what());
        return false;
    } catch (...) {
        spdlog::warn("breakdown: Havok hook unknown failure -- folding into other_ms");
        return false;
    }
}

// -------- AI / ProcessLists::Update -------------------------------------
struct AiHook {
    static void thunk(RE::ProcessLists* a_pl, float a_delta, bool a_b) {
        const auto t0 = QpcNowUs();
        _original(a_pl, a_delta, a_b);
        CpuBreakdownSampler::AddAiUs(QpcNowUs() - t0);
    }
    static inline REL::Relocation<decltype(thunk)> _original;
    // ProcessLists::Update -- speculative anchor; verify before enabling.
    static constexpr std::uint64_t kSeId = 40314;
    static constexpr std::uint64_t kAeId = 41340;
};

bool InstallAi() {
    try {
        SKSE::AllocTrampoline(14);
        REL::Relocation<std::uintptr_t> target{
            REL::RelocationID(AiHook::kSeId, AiHook::kAeId), 0x0
        };
        AiHook::_original =
            SKSE::GetTrampoline().write_call<5>(target.address(),
                                                 &AiHook::thunk);
        spdlog::info("breakdown: AI (ProcessLists::Update) hook installed");
        return true;
    } catch (const std::exception& e) {
        spdlog::warn("breakdown: AI hook failed: {} -- folding into other_ms", e.what());
        return false;
    } catch (...) {
        spdlog::warn("breakdown: AI hook unknown failure -- folding into other_ms");
        return false;
    }
}

// -------- Renderer submit -----------------------------------------------
// BSGraphics::Renderer::End (the per-frame "submit & present" boundary).
struct RenderSubmitHook {
    static void thunk() {
        const auto t0 = QpcNowUs();
        _original();
        CpuBreakdownSampler::AddRenderSubmitUs(QpcNowUs() - t0);
    }
    static inline REL::Relocation<decltype(thunk)> _original;
    // BSGraphics::Renderer::End -- speculative anchor; verify before enabling.
    static constexpr std::uint64_t kSeId = 75462;
    static constexpr std::uint64_t kAeId = 77246;
};

bool InstallRenderSubmit() {
    try {
        SKSE::AllocTrampoline(14);
        REL::Relocation<std::uintptr_t> target{
            REL::RelocationID(RenderSubmitHook::kSeId, RenderSubmitHook::kAeId), 0x0
        };
        RenderSubmitHook::_original =
            SKSE::GetTrampoline().write_call<5>(target.address(),
                                                 &RenderSubmitHook::thunk);
        spdlog::info("breakdown: render submit hook installed");
        return true;
    } catch (const std::exception& e) {
        spdlog::warn("breakdown: render submit hook failed: {} -- folding into other_ms", e.what());
        return false;
    } catch (...) {
        spdlog::warn("breakdown: render submit hook unknown failure -- folding into other_ms");
        return false;
    }
}

}  // namespace

void InstallAll(const config::CpuBreakdownConfig& a_cfg) {
    if (!a_cfg.enabled) {
        spdlog::info("breakdown: disabled in config");
        return;
    }

    CpuBreakdownSampler::EnabledMask mask{};
    if (a_cfg.havok_hook)         mask.havok = InstallHavok();
    if (a_cfg.ai_hook)            mask.ai = InstallAi();
    if (a_cfg.render_submit_hook) mask.render_submit = InstallRenderSubmit();
    // main_loop_hook is folded into the frame sampler's existing Main::Update
    // hook; nothing extra to install.

    // Papyrus and streaming flags are set by their own phases when they
    // successfully install. We merge here to preserve any already-set bits.
    auto existing = CpuBreakdownSampler::GetEnabledMask();
    mask.papyrus = existing.papyrus;
    mask.streaming = existing.streaming;
    CpuBreakdownSampler::SetEnabledMask(mask);

    spdlog::info("breakdown: enabled mask havok={} ai={} render_submit={} papyrus={} streaming={}",
                 mask.havok, mask.ai, mask.render_submit, mask.papyrus, mask.streaming);
}

void InstallStreamingHook() {
    // Implemented in the streaming_sampler phase. Stub here so the header
    // compiles and plugin.cpp doesn't fork on the flag.
}

}  // namespace skygraph::samplers::subsystem_hooks
