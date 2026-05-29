#include "samplers/subsystem_hooks.h"

#include "samplers/cpu_breakdown.h"

#include <RE/Skyrim.h>
#include <REL/Relocation.h>
#include <SKSE/SKSE.h>
#include <SKSE/Trampoline.h>

#include <spdlog/spdlog.h>

#include <cstdint>
#include <unordered_set>

namespace skygraph::samplers::subsystem_hooks {

namespace {

// Each hook is a CALL-SITE REDIRECT, structured identically:
//   1. Bracket the call with QpcNowUs() to measure inclusive subsystem time.
//   2. Add the delta to the matching CpuBreakdownSampler bucket.
//   3. Call through to the original function.
//
// CommonLibSSE-NG's trampoline (write_call/write_branch) does NOT relocate a
// function prologue -- it rewrites an *existing* `CALL rel32` / `JMP rel32`
// instruction in place and hands back the original callee. So a hook target
// must be the address of a CALL instruction (an Address Library id + the
// byte offset of that call within its enclosing function), NOT a function
// entry.
//
// SAFETY: enabling these hooks can never crash the load, regardless of the
// configured ids:
//   * KnownIdSet() lets us test whether an id exists in the loaded Address
//     Library WITHOUT calling id2offset() (which fatally report_and_fails on
//     a miss -- the dreaded "Failed to find the id" messagebox).
//   * Before patching we read the byte at the resolved address and require it
//     to be 0xE8 (CALL rel32). A function-entry prologue is never 0xE8, so a
//     wrong-but-existing id is rejected instead of corrupting the engine.
// Any failed gate logs the reason and folds that subsystem's time into
// other_ms.
//
// Default IDs by hook:
//   * render_submit -- VERIFIED (75461 SE / 77246 AE @ +0x9), from
//     OpenAnimationReplacer's present hook. Active out of the box.
//   * havok, ai -- no community-verified call-site id was found, so these
//     default to 0 and self-skip (logging why) until you supply a verified
//     call-site id+offset via
//       samplers.cpu_breakdown.<hook>_hook_ids = { id_se, id_ae, offset }
//     in skygraph.json. See docs/address-library.md.

constexpr std::uint64_t PickId(std::uint64_t a_override, std::uint64_t a_default) {
    return a_override != 0 ? a_override : a_default;
}

// Build (once) the set of every Address Library id present in the loaded
// database. REL::IDDatabase::Offset2ID copies + sorts the whole table; we walk
// it once and keep only the ids so membership tests are O(1) and -- crucially
// -- non-fatal, unlike REL::ID::offset()/id2offset().
const std::unordered_set<std::uint64_t>& KnownIdSet() {
    static const std::unordered_set<std::uint64_t> kSet = [] {
        std::unordered_set<std::uint64_t> set;
        try {
            REL::IDDatabase::Offset2ID map;
            set.reserve(map.size());
            for (const auto& entry : map) {
                set.insert(entry.id);
            }
            spdlog::info("breakdown: Address Library id index built ({} ids)",
                         set.size());
        } catch (...) {
            spdlog::warn("breakdown: could not build Address Library id index; "
                         "subsystem hooks will be skipped");
        }
        return set;
    }();
    return kSet;
}

bool IdExists(std::uint64_t a_id) {
    if (a_id == 0) {
        return false;
    }
    const auto& set = KnownIdSet();
    return set.find(a_id) != set.end();
}

// Resolve + validate + patch a CALL site. Returns the original callee address
// on success, or 0 if any gate fails (every failure is logged). The caller
// assigns the result into its typed REL::Relocation _original only on success.
template <class ThunkPtr>
std::uintptr_t TryInstallCallSite(const char* a_name,
                                  const config::HookIds& a_ids,
                                  std::uint64_t a_defaultSe,
                                  std::uint64_t a_defaultAe,
                                  std::uintptr_t a_defaultOffset,
                                  ThunkPtr a_thunk) {
    const bool isAE = REL::Module::IsAE();
    const char* rt = isAE ? "AE" : (REL::Module::IsVR() ? "VR" : "SE");
    const std::uint64_t activeId =
        PickId(isAE ? a_ids.id_ae : a_ids.id_se, isAE ? a_defaultAe : a_defaultSe);
    const std::uintptr_t offset = a_ids.offset != 0
                                      ? static_cast<std::uintptr_t>(a_ids.offset)
                                      : a_defaultOffset;

    if (activeId == 0) {
        spdlog::warn("breakdown: {} hook skipped -- no Address Library id configured "
                     "for {} (set samplers.cpu_breakdown.{}_hook_ids in skygraph.json); "
                     "folding into other_ms",
                     a_name, rt, a_name);
        return 0;
    }
    if (!IdExists(activeId)) {
        spdlog::warn("breakdown: {} hook skipped -- id {} not present in the {} "
                     "Address Library; folding into other_ms",
                     a_name, activeId, rt);
        return 0;
    }

    std::uintptr_t addr = 0;
    try {
        addr = REL::ID(activeId).address() + offset;
    } catch (...) {
        spdlog::warn("breakdown: {} hook skipped -- could not resolve id {} (+0x{:X}); "
                     "folding into other_ms",
                     a_name, activeId, offset);
        return 0;
    }

    const auto opcode = *reinterpret_cast<const std::uint8_t*>(addr);
    if (opcode != 0xE8) {
        spdlog::warn("breakdown: {} hook skipped -- target 0x{:X} (id {} +0x{:X}, {}) "
                     "is not a CALL site (opcode 0x{:02X}, expected 0xE8). The id likely "
                     "points at a function entry rather than a call site; provide a "
                     "verified call-site id+offset in skygraph.json. Folding into other_ms",
                     a_name, addr, activeId, offset, rt, opcode);
        return 0;
    }

    std::uintptr_t original = 0;
    try {
        SKSE::AllocTrampoline(14);
        original = SKSE::GetTrampoline().write_call<5>(addr, a_thunk);
    } catch (const std::exception& e) {
        spdlog::warn("breakdown: {} hook install threw at 0x{:X} ({}); "
                     "folding into other_ms",
                     a_name, addr, e.what());
        return 0;
    } catch (...) {
        spdlog::warn("breakdown: {} hook install threw at 0x{:X} (unknown); "
                     "folding into other_ms",
                     a_name, addr);
        return 0;
    }

    spdlog::info("breakdown: {} hook installed at call site 0x{:X} "
                 "(id {} +0x{:X}, {}, original callee 0x{:X})",
                 a_name, addr, activeId, offset, rt, original);
    return original;
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
    static constexpr std::uint64_t kDefaultSeId = 0;  // UNVERIFIED - set via config
    static constexpr std::uint64_t kDefaultAeId = 0;
    static constexpr std::uintptr_t kDefaultOffset = 0x0;
};

bool InstallHavok(const config::HookIds& a_ids) {
    const auto original = TryInstallCallSite(
        "havok", a_ids, HavokHook::kDefaultSeId, HavokHook::kDefaultAeId,
        HavokHook::kDefaultOffset, &HavokHook::thunk);
    if (original == 0) {
        return false;
    }
    HavokHook::_original = original;
    return true;
}

// -------- AI / ProcessLists::Update -------------------------------------
struct AiHook {
    static void thunk(RE::ProcessLists* a_pl, float a_delta, bool a_b) {
        const auto t0 = QpcNowUs();
        _original(a_pl, a_delta, a_b);
        CpuBreakdownSampler::AddAiUs(QpcNowUs() - t0);
    }
    static inline REL::Relocation<decltype(thunk)> _original;
    static constexpr std::uint64_t kDefaultSeId = 0;  // UNVERIFIED - set via config
    static constexpr std::uint64_t kDefaultAeId = 0;
    static constexpr std::uintptr_t kDefaultOffset = 0x0;
};

bool InstallAi(const config::HookIds& a_ids) {
    const auto original = TryInstallCallSite(
        "ai", a_ids, AiHook::kDefaultSeId, AiHook::kDefaultAeId,
        AiHook::kDefaultOffset, &AiHook::thunk);
    if (original == 0) {
        return false;
    }
    AiHook::_original = original;
    return true;
}

// -------- Renderer submit -----------------------------------------------
// The per-frame Present boundary -- the `call Present` site that OpenAnimation-
// Replacer hooks. IDs and offset are lifted verbatim from its HavokHooks
// (REL::VariantID(75461, 77246, ...) + VariantOffset(0x9, 0x9, 0x15)) and are
// proven in production on SE 1.5.97 and AE. NOTE: this brackets the present
// call, so the time includes driver present + any vsync/GPU wait -- a large
// "render" slice usually means GPU/present-bound rather than CPU-bound.
struct RenderSubmitHook {
    static void thunk(std::uint32_t a_arg) {
        const auto t0 = QpcNowUs();
        _original(a_arg);
        CpuBreakdownSampler::AddRenderSubmitUs(QpcNowUs() - t0);
    }
    static inline REL::Relocation<decltype(thunk)> _original;
    static constexpr std::uint64_t kDefaultSeId = 75461;   // OAR present hook (SE)
    static constexpr std::uint64_t kDefaultAeId = 77246;   // OAR present hook (AE)
    static constexpr std::uintptr_t kDefaultOffset = 0x9;  // call-site (SE/AE)
};

bool InstallRenderSubmit(const config::HookIds& a_ids) {
    const auto original = TryInstallCallSite(
        "render_submit", a_ids, RenderSubmitHook::kDefaultSeId,
        RenderSubmitHook::kDefaultAeId, RenderSubmitHook::kDefaultOffset,
        &RenderSubmitHook::thunk);
    if (original == 0) {
        return false;
    }
    RenderSubmitHook::_original = original;
    return true;
}

}  // namespace

void InstallAll(const config::CpuBreakdownConfig& a_cfg) {
    if (!a_cfg.enabled) {
        spdlog::info("breakdown: disabled in config");
        return;
    }

    spdlog::info("breakdown: installing subsystem hooks (requested havok={} ai={} "
                 "render_submit={})",
                 a_cfg.havok_hook, a_cfg.ai_hook, a_cfg.render_submit_hook);

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
