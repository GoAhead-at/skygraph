#pragma once

#include "samplers/sampler.h"

#include <atomic>
#include <thread>

namespace skygraph::samplers {

// 2Hz sampler. Uses SKSE::GetTaskInterface()->AddTask to read game state on
// the main thread (where ProcessLists, PlayerCharacter, and TESDataHandler
// require us to be), then submits a `state` record from there.
//
// Doesn't own any hooks; complements the event sources (cell_attach,
// save, mod_event) installed by event_sources.cpp.
class StateSampler : public Sampler {
public:
    StateSampler(transport::WriterThread& a_writer, double a_rateHz);

    void Start() override;
    void Stop() override;

private:
    double _rateHz;
    std::atomic<bool> _running{ false };
    std::thread _tickerThread;

    // Tries to schedule a sample on the main thread. The sample itself reads
    // game state and submits the record.
    void ScheduleSample();
};

}  // namespace skygraph::samplers
