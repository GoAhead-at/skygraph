#include "samplers/sampler.h"

#include <skygraph/protocol/messages.h>

#include <chrono>

namespace skygraph::samplers {

void Sampler::Emit(std::string_view a_type, nlohmann::json a_body) {
    a_body[skygraph::protocol::kFieldType] = std::string{ a_type };
    a_body[skygraph::protocol::kFieldTimestamp] = NowEpochSeconds();
    _writer.Submit(a_body.dump());
}

double Sampler::NowEpochSeconds() noexcept {
    using namespace std::chrono;
    return duration<double>(system_clock::now().time_since_epoch()).count();
}

}  // namespace skygraph::samplers
