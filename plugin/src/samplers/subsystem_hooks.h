#pragma once

#include "config/plugin_config.h"

namespace skygraph::samplers::subsystem_hooks {

// Installs the four CPU-breakdown sub-hooks (Havok, AI, render submit, main
// loop) based on the active config. Idempotent; safe to call once at
// kPostLoad. Returns the EnabledMask reflecting which hooks succeeded.
//
// Each hook is wrapped in its own try/catch -- a failure in one doesn't
// prevent the others from installing. The Papyrus VM hook is installed by a
// separate phase (papyrus_sampler) and registers into the same accumulator.
void InstallAll(const config::CpuBreakdownConfig& a_cfg);

// Installs the BSResource streaming hook, which is intentionally separated
// out (and feature-flagged off by default) because it's the most fragile
// single hook in the project. Called by the streaming_sampler if enabled.
void InstallStreamingHook();

}  // namespace skygraph::samplers::subsystem_hooks
