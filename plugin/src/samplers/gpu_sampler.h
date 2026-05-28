#pragma once

#include "samplers/sampler.h"

namespace skygraph::samplers {

// Stub for GPU timestamp queries. Feature-flagged off in skygraph.json by
// default. When enabled, would hook IDXGISwapChain::Present and bracket the
// frame with D3D11 timestamp queries to publish gpu_frame_ms via
// FrameSampler::PublishGpuFrameMs.
//
// Filled in fully during the streaming_sampler / packaging phase where we
// also touch the renderer hook chain. For now the class exists so plugin.cpp
// can compile against it and the config field is honored.
class GpuSampler : public Sampler {
public:
    explicit GpuSampler(transport::WriterThread& a_writer);

    void Start() override;
    void Stop() override;
};

}  // namespace skygraph::samplers
