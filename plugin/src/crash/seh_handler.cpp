#include "crash/seh_handler.h"

#include "transport/rolling_recorder.h"
#include "transport/writer_thread.h"

#include <skygraph/protocol/messages.h>

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include <fmt/format.h>

#include <Windows.h>

#include <atomic>
#include <chrono>
#include <string>

namespace skygraph::crash {

namespace {

std::atomic<transport::WriterThread*> g_writer{ nullptr };
std::atomic<transport::RollingRecorder*> g_recorder{ nullptr };
std::atomic<bool> g_handled{ false };

bool IsFatal(DWORD a_code) {
    switch (a_code) {
        case EXCEPTION_ACCESS_VIOLATION:
        case EXCEPTION_ILLEGAL_INSTRUCTION:
        case EXCEPTION_PRIV_INSTRUCTION:
        case EXCEPTION_STACK_OVERFLOW:
        case EXCEPTION_INT_DIVIDE_BY_ZERO:
        case EXCEPTION_NONCONTINUABLE_EXCEPTION:
        case EXCEPTION_FLT_DIVIDE_BY_ZERO:
        case EXCEPTION_FLT_INVALID_OPERATION:
        case EXCEPTION_FLT_STACK_CHECK:
            return true;
        default:
            return false;
    }
}

std::string ModuleAtAddress(void* a_addr) {
    HMODULE mod = nullptr;
    if (!GetModuleHandleExA(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS
                | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCSTR>(a_addr), &mod)) {
        return {};
    }
    char buf[MAX_PATH] = {};
    GetModuleFileNameA(mod, buf, MAX_PATH);
    // Just the leaf name.
    std::string s = buf;
    auto pos = s.find_last_of("\\/");
    return pos == std::string::npos ? s : s.substr(pos + 1);
}

LONG WINAPI Handler(EXCEPTION_POINTERS* a_ep) {
    if (!a_ep || !a_ep->ExceptionRecord) {
        return EXCEPTION_CONTINUE_SEARCH;
    }
    const DWORD code = a_ep->ExceptionRecord->ExceptionCode;
    if (!IsFatal(code)) return EXCEPTION_CONTINUE_SEARCH;
    if (g_handled.exchange(true)) {
        // Already wrote a crash record this process; don't re-enter.
        return EXCEPTION_CONTINUE_SEARCH;
    }

    void* rip = a_ep->ExceptionRecord->ExceptionAddress;
    const auto codeStr = fmt::format("0x{:08X}", code);
    const auto ripStr = fmt::format("0x{:016X}",
                                    reinterpret_cast<std::uintptr_t>(rip));
    const auto modStr = ModuleAtAddress(rip);

    using namespace std::chrono;
    nlohmann::json j = {
        { skygraph::protocol::kFieldType, skygraph::protocol::msg::kEventCrash },
        { skygraph::protocol::kFieldTimestamp,
          duration<double>(system_clock::now().time_since_epoch()).count() },
        { "code", codeStr },
        { "rip", ripStr },
        { "module", modStr },
    };

    if (auto* w = g_writer.load(std::memory_order_acquire)) {
        try { w->Submit(j.dump()); } catch (...) {}
    }
    if (auto* r = g_recorder.load(std::memory_order_acquire)) {
        try { r->RotateNow(); } catch (...) {}
    }

    spdlog::critical("CRASH: {} at {} ({})", codeStr, ripStr, modStr);
    spdlog::default_logger()->flush();

    // Pass through so other crash handlers (NetScriptFramework, Crash Logger,
    // Windows default) get to do their job.
    return EXCEPTION_CONTINUE_SEARCH;
}

}  // namespace

void Install(transport::WriterThread* a_writer,
             transport::RollingRecorder* a_recorder) noexcept {
    g_writer.store(a_writer, std::memory_order_release);
    g_recorder.store(a_recorder, std::memory_order_release);

    // Add as the LAST handler so we run first (Windows fires VEHs in reverse
    // registration order, FILO). Calling SKSEPluginLoad late ensures third-
    // party plugins that install their own VEHs run after ours.
    AddVectoredExceptionHandler(/*First=*/1, &Handler);
    spdlog::info("crash: VEH installed");
}

}  // namespace skygraph::crash
