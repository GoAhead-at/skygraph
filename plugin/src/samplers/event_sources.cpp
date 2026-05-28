#include "samplers/event_sources.h"

#include "diagnostics/latest_cache.h"

#include <skygraph/protocol/messages.h>

#include <RE/Skyrim.h>
#include <SKSE/SKSE.h>

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include <Windows.h>

#include <chrono>
#include <mutex>
#include <string>
#include <unordered_map>

namespace skygraph::samplers::event_sources {

namespace {

transport::WriterThread* g_writer = nullptr;

double Now() {
    using namespace std::chrono;
    return duration<double>(system_clock::now().time_since_epoch()).count();
}

std::int64_t QpcNowUs() {
    static const std::int64_t freq = []() {
        LARGE_INTEGER f{};
        QueryPerformanceFrequency(&f);
        return f.QuadPart;
    }();
    LARGE_INTEGER c{};
    QueryPerformanceCounter(&c);
    return c.QuadPart * 1'000'000 / (freq ? freq : 1);
}

void Emit(std::string_view a_type, nlohmann::json a_body) {
    if (!g_writer) return;
    a_body[skygraph::protocol::kFieldType] = std::string{ a_type };
    a_body[skygraph::protocol::kFieldTimestamp] = Now();
    g_writer->Submit(a_body.dump());
}

// --------- Cell attach/detach with duration bracketing ------------------
//
// Per-cell start timestamps so we can compute duration on detach (or on the
// matching "attach complete" callback). We accept that very long-lived
// cells may sit in this map; cell turnover is sparse enough that it doesn't
// matter.
struct CellTimings {
    std::mutex mtx;
    std::unordered_map<RE::FormID, std::int64_t> attach_start_us;
};
CellTimings& Timings() { static CellTimings t; return t; }

class CellSink final
    : public RE::BSTEventSink<RE::TESCellFullyLoadedEvent>,
      public RE::BSTEventSink<RE::TESCellAttachDetachEvent> {
public:
    static CellSink* GetSingleton() { static CellSink s; return &s; }

    RE::BSEventNotifyControl ProcessEvent(
        const RE::TESCellAttachDetachEvent* a_event,
        RE::BSTEventSource<RE::TESCellAttachDetachEvent>*) override {
        if (!a_event || !a_event->reference) {
            return RE::BSEventNotifyControl::kContinue;
        }
        auto* cell = a_event->reference->GetParentCell();
        if (!cell) return RE::BSEventNotifyControl::kContinue;

        const auto id = cell->GetFormID();
        if (a_event->attached) {
            std::lock_guard lk{ Timings().mtx };
            Timings().attach_start_us[id] = QpcNowUs();
            std::string name = cell->GetFormEditorID();
            if (name.empty()) name = cell->GetName();
            diagnostics::LatestCache::Get().NoteCellAttachStart(name);
        } else {
            std::int64_t startUs = 0;
            {
                std::lock_guard lk{ Timings().mtx };
                if (auto it = Timings().attach_start_us.find(id);
                    it != Timings().attach_start_us.end()) {
                    startUs = it->second;
                    Timings().attach_start_us.erase(it);
                }
            }
            const double durMs =
                startUs ? (QpcNowUs() - startUs) / 1000.0 : 0.0;
            std::string name = cell->GetFormEditorID();
            if (name.empty()) name = cell->GetName();
            Emit(skygraph::protocol::msg::kEventCellDetach, {
                { "cell", name },
                { "duration_ms", durMs },
            });
        }
        return RE::BSEventNotifyControl::kContinue;
    }

    RE::BSEventNotifyControl ProcessEvent(
        const RE::TESCellFullyLoadedEvent* a_event,
        RE::BSTEventSource<RE::TESCellFullyLoadedEvent>*) override {
        if (!a_event || !a_event->cell) {
            return RE::BSEventNotifyControl::kContinue;
        }
        const auto id = a_event->cell->GetFormID();
        std::int64_t startUs = 0;
        {
            std::lock_guard lk{ Timings().mtx };
            if (auto it = Timings().attach_start_us.find(id);
                it != Timings().attach_start_us.end()) {
                startUs = it->second;
            }
        }
        const double durMs =
            startUs ? (QpcNowUs() - startUs) / 1000.0 : 0.0;
        std::string name = a_event->cell->GetFormEditorID();
        if (name.empty()) name = a_event->cell->GetName();
        diagnostics::LatestCache::Get().NoteCellAttachComplete(name);
        Emit(skygraph::protocol::msg::kEventCellAttach, {
            { "cell", name },
            { "duration_ms", durMs },
        });
        return RE::BSEventNotifyControl::kContinue;
    }
};

void InstallCellSinks() {
    auto* src1 = RE::ScriptEventSourceHolder::GetSingleton();
    if (!src1) return;
    src1->AddEventSink<RE::TESCellAttachDetachEvent>(CellSink::GetSingleton());
    src1->AddEventSink<RE::TESCellFullyLoadedEvent>(CellSink::GetSingleton());
}

// --------- Mod event traffic (SKSE script messaging) --------------------
class ModEventSink final : public RE::BSTEventSink<SKSE::ModCallbackEvent> {
public:
    static ModEventSink* GetSingleton() { static ModEventSink s; return &s; }

    RE::BSEventNotifyControl ProcessEvent(
        const SKSE::ModCallbackEvent* a_event,
        RE::BSTEventSource<SKSE::ModCallbackEvent>*) override {
        if (!a_event) return RE::BSEventNotifyControl::kContinue;
        Emit(skygraph::protocol::msg::kEventModEvent, {
            { "name", a_event->eventName.c_str() },
            { "sender", a_event->sender ? a_event->sender->GetName() : "" },
            { "str_arg", a_event->strArg.c_str() },
            { "num_arg", a_event->numArg },
        });
        return RE::BSEventNotifyControl::kContinue;
    }
};

void InstallModEventSink() {
    auto* messaging = SKSE::GetModCallbackEventSource();
    if (!messaging) return;
    messaging->AddEventSink(ModEventSink::GetSingleton());
}

// --------- One-shot load order ------------------------------------------
void EmitLoadOrder() {
    auto* dh = RE::TESDataHandler::GetSingleton();
    if (!dh) return;
    nlohmann::json files = nlohmann::json::array();
    for (const auto& f : dh->files) {
        if (!f) continue;
        nlohmann::json masters = nlohmann::json::array();
        for (const auto& m : f->masters) {
            // RE::TESFile::masters is iterable over `const char*` entries.
            if (m) {
                masters.push_back(std::string{ m });
            }
        }
        files.push_back({
            { "name", std::string{ f->fileName } },
            { "masters", std::move(masters) },
        });
    }
    Emit(skygraph::protocol::msg::kPlugins, { { "files", std::move(files) } });
}

}  // namespace

void InstallAll(transport::WriterThread& a_writer) {
    g_writer = &a_writer;
    try { InstallCellSinks(); } catch (const std::exception& e) {
        spdlog::warn("events: cell sinks failed: {}", e.what());
    }
    try { InstallModEventSink(); } catch (const std::exception& e) {
        spdlog::warn("events: mod-event sink failed: {}", e.what());
    }
    try { EmitLoadOrder(); } catch (const std::exception& e) {
        spdlog::warn("events: load-order emit failed: {}", e.what());
    }
    spdlog::info("events: sinks installed");
}

// --------- Save / autosave ----------------------------------------------
//
// We use the SKSE messaging interface rather than RE-level save events because
// the message types here (kSaveGame, kPostLoadGame) are guaranteed stable
// across CommonLibSSE-NG versions. The dispatcher is registered once in
// plugin.cpp; we just expose a free function it can forward "save" messages
// to. These must live in the *named* namespace so plugin.cpp's external
// reference resolves at link time.
void EmitSaveEvent(const char* a_name) {
    Emit(skygraph::protocol::msg::kEventSave, {
        { "name", a_name ? a_name : "" },
    });
}

void EmitAutosaveEvent() {
    Emit(skygraph::protocol::msg::kEventAutosave, {});
}

}  // namespace skygraph::samplers::event_sources
