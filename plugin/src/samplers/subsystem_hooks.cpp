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
//   1. Bracket the call with QpcNowUs() to measure inclusive subsystem time.
//   2. Add the delta to the matching CpuBreakdownSampler bucket.
//   3. Call through to the original function.
//
// IMPORTANT: the kDefault*Id integers below are SPECULATIVE placeholders.
// They were chosen from memory of common SKSE patterns and have NOT been
// verified against any specific runtime's Address Library database. Enabling
// a hook with the wrong ID will cause CommonLibSSE-NG to pop a fatal
// "Failed to find the id within the address library" messagebox during plugin
// load and Skyrim won't start.
//
// To override an ID without recompiling, set the matching
// samplers.cpu_breakdown.<hook>_hook_ids = { id_se, id_ae } block in
// skygraph.json. A non-zero override wins; a zero (or missing) field falls
// back to the kDefault*Id baked in here. See docs/address-library.md for
// how to look up the right IDs for your runtime.
//
// If the install throws (and our catch{} actually catches it -- which is
// not guaranteed; see plugin.cpp note), that subsystem's CPU time silently
// rolls into other_ms.

constexpr std::uint64_t PickId(std::uint64_t a_override, std::uint64_t a_default) {
    return a_override != 0 ? a_override : a_default;
}

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
    static constexpr std::uint64_t kDefaultSeId = 76202;  // bhkWorld::Update
    static constexpr std::uint64_t kDefaultAeId = 77985;
};

bool InstallHavok(const config::HookIds& a_ids) {
    const auto seId = PickId(a_ids.id_se, HavokHook::kDefaultSeId);
    const auto aeId = PickId(a_ids.id_ae, HavokHook::kDefaultAeId);
    try {
        SKSE::AllocTrampoline(14);
        REL::Relocation<std::uintptr_t> target{
            REL::RelocationID(seId, aeId), 0x0
        };
        HavokHook::_original =
            SKSE::GetTrampoline().write_call<5>(target.address(),
                                                 &HavokHook::thunk);
        spdlog::info("breakdown: Havok hook installed (id_se={} id_ae={})", seId, aeId);
        return true;
    } catch (const std::exception& e) {
        spdlog::warn("breakdown: Havok hook failed (id_se={} id_ae={}): {} -- "
                     "folding into other_ms", seId, aeId, e.what());
        return false;
    } catch (...) {
        spdlog::warn("breakdown: Havok hook unknown failure (id_se={} id_ae={}) -- "
                     "folding into other_ms", seId, aeId);
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
    static constexpr std::uint64_t kDefaultSeId = 40314;  // ProcessLists::Update
    static constexpr std::uint64_t kDefaultAeId = 41340;
};

bool InstallAi(const config::HookIds& a_ids) {
    const auto seId = PickId(a_ids.id_se, AiHook::kDefaultSeId);
    const auto aeId = PickId(a_ids.id_ae, AiHook::kDefaultAeId);
    try {
        SKSE::AllocTrampoline(14);
        REL::Relocation<std::uintptr_t> target{
            REL::RelocationID(seId, aeId), 0x0
        };
        AiHook::_original =
            SKSE::GetTrampoline().write_call<5>(target.address(),
                                                 &AiHook::thunk);
        spdlog::info("breakdown: AI hook installed (id_se={} id_ae={})", seId, aeId);
        return true;
    } catch (const std::exception& e) {
        spdlog::warn("breakdown: AI hook failed (id_se={} id_ae={}): {} -- "
                     "folding into other_ms", seId, aeId, e.what());
        return false;
    } catch (...) {
        spdlog::warn("breakdown: AI hook unknown failure (id_se={} id_ae={}) -- "
                     "folding into other_ms", seId, aeId);
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
    static constexpr std::uint64_t kDefaultSeId = 75462;  // BSGraphics::Renderer::End
    static constexpr std::uint64_t kDefaultAeId = 77246;
};

bool InstallRenderSubmit(const config::HookIds& a_ids) {
    const auto seId = PickId(a_ids.id_se, RenderSubmitHook::kDefaultSeId);
    const auto aeId = PickId(a_ids.id_ae, RenderSubmitHook::kDefaultAeId);
    try {
        SKSE::AllocTrampoline(14);
        REL::Relocation<std::uintptr_t> target{
            REL::RelocationID(seId, aeId), 0x0
        };
        RenderSubmitHook::_original =
            SKSE::GetTrampoline().write_call<5>(target.address(),
                                                 &RenderSubmitHook::thunk);
        spdlog::info("breakdown: render submit hook installed (id_se={} id_ae={})",
                     seId, aeId);
        return true;
    } catch (const std::exception& e) {
        spdlog::warn("breakdown: render submit hook failed (id_se={} id_ae={}): {} -- "
                     "folding into other_ms", seId, aeId, e.what());
        return false;
    } catch (...) {
        spdlog::warn("breakdown: render submit hook unknown failure "
                     "(id_se={} id_ae={}) -- folding into other_ms", seId, aeId);
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
    if (a_cfg.havok_hook)         mask.havok = InstallHavok(a_cfg.havok_hook_ids);
    if (a_cfg.ai_hook)            mask.ai = InstallAi(a_cfg.ai_hook_ids);
    if (a_cfg.render_submit_hook) mask.render_submit = InstallRenderSubmit(a_cfg.render_submit_hook_ids);
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
