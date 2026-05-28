#pragma once

#include "samplers/sampler.h"

#include <atomic>
#include <thread>

namespace skygraph::samplers {

// Two responsibilities:
//   1. Install the VM stack-execution hook (Address-Library resolved) and
//      route per-stack timing into PapyrusAttribution. Also feeds the
//      cpu_breakdown's papyrus_us bucket so the breakdown chart attributes
//      VM time correctly.
//   2. Run a background thread that, at snapshot_hz, samples the VM
//      singleton for active/suspended/latent counts (emits papyrus.snapshot)
//      and drains the attribution map's top-N hot scripts (emits papyrus.top).
class PapyrusSampler : public Sampler {
public:
    PapyrusSampler(transport::WriterThread& a_writer,
                   double a_snapshotHz,
                   int a_topN,
                   bool a_installVmHook);

    void Start() override;
    void Stop() override;

private:
    double _snapshotHz;
    int _topN;
    bool _installVmHook;
    std::atomic<bool> _running{ false };
    std::atomic<bool> _hookInstalled{ false };
    std::thread _thread;

    bool InstallHook();
    void SnapshotLoop();
    void EmitSnapshot();
    void EmitTop();
};

}  // namespace skygraph::samplers
