#pragma once

#include "transport/writer_thread.h"

#include <nlohmann/json.hpp>

#include <string>
#include <string_view>

namespace skygraph::samplers {

// Common base for all samplers. The Submit helper centralizes the timestamp
// + type field decoration so individual samplers stay tiny.
class Sampler {
public:
    Sampler(std::string a_name, transport::WriterThread& a_writer)
        : _name{ std::move(a_name) }, _writer{ a_writer } {}

    virtual ~Sampler() = default;

    Sampler(const Sampler&) = delete;
    Sampler& operator=(const Sampler&) = delete;

    virtual void Start() = 0;
    virtual void Stop() = 0;

    std::string_view Name() const noexcept { return _name; }

protected:
    transport::WriterThread& Writer() noexcept { return _writer; }

    // Emit a record. Stamps `type` and `t` automatically.
    void Emit(std::string_view a_type, nlohmann::json a_body);

    // Wall-clock seconds since unix epoch (double precision).
    static double NowEpochSeconds() noexcept;

private:
    std::string _name;
    transport::WriterThread& _writer;
};

}  // namespace skygraph::samplers
