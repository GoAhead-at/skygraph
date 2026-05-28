#include "samplers/state_sampler.h"

#include "diagnostics/latest_cache.h"

#include <skygraph/protocol/messages.h>

#include <RE/Skyrim.h>
#include <SKSE/SKSE.h>

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include <chrono>
#include <thread>

namespace skygraph::samplers {

namespace {

std::uint32_t SizeOrZero(const auto& a_container) {
    try {
        return static_cast<std::uint32_t>(a_container.size());
    } catch (...) {
        return 0;
    }
}

void EmitStateOnMainThread(transport::WriterThread& a_writer) {
    auto* dh = RE::TESDataHandler::GetSingleton();
    auto* pl = RE::ProcessLists::GetSingleton();
    auto* player = RE::PlayerCharacter::GetSingleton();
    if (!player) return;

    std::string cellName;
    std::string worldspaceName;
    float pos[3] = { 0, 0, 0 };
    std::uint32_t loadedRefs = 0;

    if (auto* cell = player->GetParentCell()) {
        cellName = cell->GetFormEditorID();
        if (cellName.empty()) cellName = cell->GetName();
    }
    if (auto* ws = player->GetWorldspace()) {
        worldspaceName = ws->GetFormEditorID();
        if (worldspaceName.empty()) worldspaceName = ws->GetName();
    }
    const auto p = player->GetPosition();
    pos[0] = p.x; pos[1] = p.y; pos[2] = p.z;

    if (dh) loadedRefs = SizeOrZero(dh->GetFormArray<RE::TESObjectREFR>());

    std::uint32_t hi = 0, mh = 0, ml = 0, lo = 0;
    if (pl) {
        hi = SizeOrZero(pl->highActorHandles);
        mh = SizeOrZero(pl->middleHighActorHandles);
        ml = SizeOrZero(pl->middleLowActorHandles);
        lo = SizeOrZero(pl->lowActorHandles);
    }

    diagnostics::LatestCache::Get().UpdateState(
        cellName, hi, mh, ml, lo, loadedRefs);

    using namespace std::chrono;
    nlohmann::json j = {
        { skygraph::protocol::kFieldType, skygraph::protocol::msg::kState },
        { skygraph::protocol::kFieldTimestamp,
          duration<double>(system_clock::now().time_since_epoch()).count() },
        { "cell", cellName },
        { "worldspace", worldspaceName },
        { "player_pos", { pos[0], pos[1], pos[2] } },
        { "actor_counts", { {"high", hi}, {"mid_high", mh},
                            {"mid_low", ml}, {"low", lo} } },
        { "loaded_refs", loadedRefs },
    };
    a_writer.Submit(j.dump());
}

}  // namespace

StateSampler::StateSampler(transport::WriterThread& a_writer, double a_rateHz)
    : Sampler{ "state", a_writer },
      _rateHz{ a_rateHz <= 0.0 ? 2.0 : a_rateHz } {}

void StateSampler::Start() {
    bool expected = false;
    if (!_running.compare_exchange_strong(expected, true)) return;

    _tickerThread = std::thread{ [this] {
        using namespace std::chrono;
        using namespace std::chrono_literals;
        const auto period = duration_cast<steady_clock::duration>(
            duration<double>(1.0 / _rateHz));
        auto next = steady_clock::now();

        while (_running.load(std::memory_order_acquire)) {
            const auto now = steady_clock::now();
            if (now >= next) {
                ScheduleSample();
                next += period;
                if (next < now) next = now + period;
            } else {
                std::this_thread::sleep_for(20ms);
            }
        }
    } };
    spdlog::info("state: sampler started (hz={})", _rateHz);
}

void StateSampler::Stop() {
    if (!_running.exchange(false)) return;
    if (_tickerThread.joinable()) _tickerThread.join();
}

void StateSampler::ScheduleSample() {
    auto* tasks = SKSE::GetTaskInterface();
    if (!tasks) return;
    auto& writer = Writer();
    tasks->AddTask([&writer] { EmitStateOnMainThread(writer); });
}

}  // namespace skygraph::samplers
