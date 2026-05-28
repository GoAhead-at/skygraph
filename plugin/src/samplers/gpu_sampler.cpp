#include "samplers/gpu_sampler.h"

#include <spdlog/spdlog.h>

namespace skygraph::samplers {

GpuSampler::GpuSampler(transport::WriterThread& a_writer)
    : Sampler{ "gpu", a_writer } {}

void GpuSampler::Start() {
    spdlog::info("gpu: feature-flagged stub active; gpu_frame_ms will remain 0 "
                 "until DXGI Present hook is enabled");
}

void GpuSampler::Stop() {}

}  // namespace skygraph::samplers
