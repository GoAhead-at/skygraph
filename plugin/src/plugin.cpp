// Skygraph SKSE plugin entry point.
//
// The foundation phase wires up the spine: configure logging, load config,
// start the pipe server, start the writer thread, and kick off a tiny
// heartbeat thread that emits a `heartbeat` record every second so we can
// prove end-to-end connectivity from the viewer side.
//
// Subsequent phases (basic_metrics, cpu_breakdown, papyrus, ...) plug their
// samplers into the same WriterThread::Submit pipeline.

#include "logging.h"
#include "config/plugin_config.h"
#include "crash/seh_handler.h"
#include "diagnostics/stutter_flagger.h"
#include "samplers/cpu_breakdown.h"
#include "samplers/event_sources.h"
#include "samplers/frame_sampler.h"
#include "samplers/gpu_sampler.h"
#include "samplers/memory_sampler.h"
#include "samplers/papyrus_sampler.h"
#include "samplers/state_sampler.h"
#include "samplers/streaming_sampler.h"
#include "samplers/subsystem_hooks.h"
#include "transport/pipe_server.h"
#include "transport/rolling_recorder.h"
#include "transport/writer_thread.h"

#include <skygraph/protocol/messages.h>
#include <skygraph/protocol/pipe.h>
#include <skygraph/protocol/version.h>

#include <SKSE/SKSE.h>
#include <RE/Skyrim.h>

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include <Windows.h>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <memory>
#include <string_view>
#include <thread>

using namespace std::string_view_literals;

namespace {

using json = nlohmann::json;

// Module-level singletons, initialized in SKSEPluginLoad and torn down at
// process exit (we deliberately leak on game-exit -- SKSE plugins do this).
std::unique_ptr<skygraph::transport::PipeServer> g_pipe;
std::unique_ptr<skygraph::transport::WriterThread> g_writer;
std::unique_ptr<skygraph::transport::RollingRecorder> g_recorder;
std::unique_ptr<skygraph::samplers::FrameSampler> g_frameSampler;
std::unique_ptr<skygraph::samplers::MemorySampler> g_memorySampler;
std::unique_ptr<skygraph::samplers::GpuSampler> g_gpuSampler;
std::unique_ptr<skygraph::samplers::CpuBreakdownSampler> g_breakdownSampler;
std::unique_ptr<skygraph::samplers::PapyrusSampler> g_papyrusSampler;
std::unique_ptr<skygraph::samplers::StateSampler> g_stateSampler;
std::unique_ptr<skygraph::samplers::StreamingSampler> g_streamingSampler;
std::atomic<bool> g_heartbeatRunning{ false };
std::thread g_heartbeatThread;
skygraph::config::PluginConfig g_config;

double NowEpochSeconds() {
    using namespace std::chrono;
    return duration<double>(system_clock::now().time_since_epoch()).count();
}

std::filesystem::path PluginDllDirectory() {
    HMODULE mod = nullptr;
    GetModuleHandleExA(
        GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS
            | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCSTR>(&PluginDllDirectory),
        &mod);

    char buf[MAX_PATH] = {};
    GetModuleFileNameA(mod, buf, MAX_PATH);
    return std::filesystem::path{ buf }.parent_path();
}

spdlog::level::level_enum LogLevelFromString(std::string_view s) {
    if (s == "trace") return spdlog::level::trace;
    if (s == "debug") return spdlog::level::debug;
    if (s == "info") return spdlog::level::info;
    if (s == "warn") return spdlog::level::warn;
    if (s == "error") return spdlog::level::err;
    if (s == "critical") return spdlog::level::critical;
    return spdlog::level::info;
}

void EmitHello() {
    using namespace skygraph::protocol;
    json j = {
        { kFieldType, msg::kHello },
        { kFieldTimestamp, NowEpochSeconds() },
        { "protocol", { { "major", kProtocolMajor }, { "minor", kProtocolMinor } } },
        { "product", {
            { "name", kProductName },
            { "version", "0.1.0" }
        } },
    };
    // Game runtime version, best-effort. Post-SKSE::Init the only stable
    // way to read the runtime version is via the REL Module singleton.
    try {
        j["game_runtime"] = std::string{ REL::Module::get().version().string() };
    } catch (...) {
        // Module not initialized yet -- omit and move on.
    }
    g_writer->Submit(j.dump());
}

void HandleCommand(std::string_view a_line) {
    try {
        auto j = json::parse(a_line);
        auto cmd = j.value(skygraph::protocol::cmd::kField, std::string{});
        if (cmd == skygraph::protocol::cmd::kPing) {
            // Reply with an immediate heartbeat so the viewer can measure RTT.
            json reply = {
                { skygraph::protocol::kFieldType,
                  skygraph::protocol::msg::kHeartbeat },
                { skygraph::protocol::kFieldTimestamp, NowEpochSeconds() },
                { "reply_to", "ping" },
            };
            g_writer->Submit(reply.dump());
        } else if (cmd == skygraph::protocol::cmd::kSaveSession) {
            const auto name = j.value("name", std::string{ "session" });
            if (g_recorder) {
                if (auto pinned = g_recorder->PinCurrent(name)) {
                    spdlog::info("cmd: pinned session to '{}'", pinned->string());
                    json reply = {
                        { skygraph::protocol::kFieldType, "ack" },
                        { skygraph::protocol::kFieldTimestamp, NowEpochSeconds() },
                        { "for_cmd", "save_session" },
                        { "path", pinned->string() },
                    };
                    g_writer->Submit(reply.dump());
                } else {
                    spdlog::warn("cmd: save_session failed");
                }
            } else {
                spdlog::warn("cmd: save_session requested but recorder is disabled");
            }
        } else {
            spdlog::warn("cmd: unknown command '{}'", cmd);
        }
    } catch (const std::exception& e) {
        spdlog::warn("cmd: parse error: {}", e.what());
    }
}

void HeartbeatLoop() {
    using namespace std::chrono;
    using namespace std::chrono_literals;

    const auto start = steady_clock::now();
    std::uint64_t seq = 0;
    while (g_heartbeatRunning.load(std::memory_order_acquire)) {
        const double uptime = duration<double>(steady_clock::now() - start).count();
        json j = {
            { skygraph::protocol::kFieldType,
              skygraph::protocol::msg::kHeartbeat },
            { skygraph::protocol::kFieldTimestamp, NowEpochSeconds() },
            { "seq", seq++ },
            { "uptime_sec", uptime },
            { "clients", g_pipe ? g_pipe->ConnectedClients() : 0u },
        };
        g_writer->Submit(j.dump());
        std::this_thread::sleep_for(1s);
    }
}

void OnSkseMessage(SKSE::MessagingInterface::Message* a_msg) {
    if (!a_msg) return;
    switch (a_msg->type) {
        case SKSE::MessagingInterface::kPostLoad:
            spdlog::info("skse: kPostLoad");
            // Hooks (Trampoline-based) must be installed at kPostLoad so we
            // have a trampoline pool available and the game's binary is fully
            // mapped.
            if (g_config.frame.enabled && g_frameSampler) {
                g_frameSampler->Start();
            }
            if (g_config.cpu_breakdown.enabled) {
                skygraph::samplers::subsystem_hooks::InstallAll(g_config.cpu_breakdown);
            }
            if (g_config.papyrus.enabled) {
                g_papyrusSampler = std::make_unique<skygraph::samplers::PapyrusSampler>(
                    *g_writer, g_config.papyrus.snapshot_hz, g_config.papyrus.top_n,
                    g_config.papyrus.vm_hook);
                g_papyrusSampler->Start();
            }
            if (g_config.streaming.enabled) {
                g_streamingSampler = std::make_unique<skygraph::samplers::StreamingSampler>(
                    *g_writer, g_config.streaming.hitch_threshold_ms);
                g_streamingSampler->Start();
            }
            if (g_config.render_gpu_timestamps.enabled && g_gpuSampler) {
                g_gpuSampler->Start();
            }
            break;
        case SKSE::MessagingInterface::kDataLoaded:
            spdlog::info("skse: kDataLoaded");
            // Form-based event sources need TESDataHandler, which is only
            // valid after kDataLoaded.
            if (g_config.events.enabled && g_writer) {
                skygraph::samplers::event_sources::InstallAll(*g_writer);
            }
            if (g_config.state.enabled && g_writer) {
                g_stateSampler = std::make_unique<skygraph::samplers::StateSampler>(
                    *g_writer, g_config.state.rate_hz);
                g_stateSampler->Start();
            }
            break;
        case SKSE::MessagingInterface::kSaveGame:
            if (g_config.events.enabled) {
                skygraph::samplers::event_sources::EmitSaveEvent(
                    static_cast<const char*>(a_msg->data));
            }
            break;
        case SKSE::MessagingInterface::kPostLoadGame:
        case SKSE::MessagingInterface::kPreLoadGame:
            break;
        default:
            break;
    }
}

}  // namespace

// SKSEPluginInfo: CommonLibSSE-NG v3.7.0 PluginDeclarationInfo layout.
// Address Library + Independent struct layout = AE-compatible across runtimes.
SKSEPluginInfo(
    .Version = REL::Version{
        skygraph::protocol::kProductVersionMajor,
        skygraph::protocol::kProductVersionMinor,
        skygraph::protocol::kProductVersionPatch,
        0
    },
    .Name = "skygraph"sv,
    .Author = "skygraph"sv,
    .SupportEmail = ""sv,
    .StructCompatibility = SKSE::StructCompatibility::Independent,
    .RuntimeCompatibility = SKSE::VersionIndependence::AddressLibrary
)

SKSEPluginLoad(const SKSE::LoadInterface* a_skse) {
    SKSE::Init(a_skse);

    const auto dllDir = PluginDllDirectory();
    g_config = skygraph::config::LoadOrDefault(dllDir);

    skygraph::logging::Init("skygraph", LogLevelFromString(g_config.log.level));
    spdlog::info("skygraph plugin {}.{}.{} (protocol {}.{}) loading",
                 skygraph::protocol::kProductVersionMajor,
                 skygraph::protocol::kProductVersionMinor,
                 skygraph::protocol::kProductVersionPatch,
                 skygraph::protocol::kProtocolMajor,
                 skygraph::protocol::kProtocolMinor);

    g_pipe = std::make_unique<skygraph::transport::PipeServer>(
        g_config.transport.pipe_name,
        skygraph::protocol::kPipeMaxInstances,
        skygraph::protocol::kPipeBufferBytes,
        &HandleCommand);
    g_writer = std::make_unique<skygraph::transport::WriterThread>(
        static_cast<std::size_t>(g_config.transport.writer_queue_capacity),
        *g_pipe);

    g_pipe->Start();
    g_writer->Start();

    if (g_config.recorder.enabled) {
        g_recorder = std::make_unique<skygraph::transport::RollingRecorder>(
            g_config.recorder.rolling_minutes, g_config.recorder.directory);
        if (g_recorder->Start()) {
            g_writer->AttachRecorder(g_recorder.get());
        }
    }

    // Samplers that don't need game hooks start immediately. Hook-based
    // samplers (frame, gpu) install at kPostLoad below.
    if (g_config.memory.enabled || g_config.memory_pressure.enabled) {
        g_memorySampler = std::make_unique<skygraph::samplers::MemorySampler>(
            *g_writer,
            g_config.memory.enabled ? g_config.memory.rate_hz : 0.0,
            g_config.memory_pressure.enabled ? g_config.memory_pressure.rate_hz : 0.0);
        g_memorySampler->Start();
    }
    if (g_config.frame.enabled) {
        g_frameSampler = std::make_unique<skygraph::samplers::FrameSampler>(*g_writer);
    }
    if (g_config.cpu_breakdown.enabled) {
        g_breakdownSampler = std::make_unique<skygraph::samplers::CpuBreakdownSampler>(*g_writer);
        g_breakdownSampler->Start();
    }
    skygraph::diagnostics::StutterFlagger::Configure(
        g_config.stutter_flagger, g_writer.get());
    if (g_config.render_gpu_timestamps.enabled) {
        g_gpuSampler = std::make_unique<skygraph::samplers::GpuSampler>(*g_writer);
    }

    auto* messaging = SKSE::GetMessagingInterface();
    if (messaging) {
        messaging->RegisterListener(&OnSkseMessage);
    }

    // Emit hello immediately; clients connecting later will receive subsequent
    // ones via OnConnect (added in a later phase). For now any client that
    // attaches before the first heartbeat will see only heartbeats.
    EmitHello();

    g_heartbeatRunning.store(true, std::memory_order_release);
    g_heartbeatThread = std::thread{ &HeartbeatLoop };

    // SEH/VEH crash handler installed LAST so we run first in the VEH chain
    // (Windows fires VEHs in FILO order). Chains through; doesn't displace
    // NetScriptFramework / Crash Logger / etc.
    if (g_config.crash_handler.enabled) {
        skygraph::crash::Install(g_writer.get(), g_recorder.get());
    }

    spdlog::info("skygraph plugin ready");
    return true;
}
