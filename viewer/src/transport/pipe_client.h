#pragma once

#include "transport/ndjson_source.h"

#include <atomic>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>

namespace skygraph::viewer {

// Pipe-backed NDJSON source. Connects on Start(); on disconnect or error
// retries with exponential backoff until Stop() is called. Outbound commands
// are serialized through a single mutex.
class PipeClient : public NdjsonSource {
public:
    explicit PipeClient(std::string a_pipeName);
    ~PipeClient() override;

    void Start() override;
    void Stop() override;

    // Best-effort send of a single JSON line (newline auto-appended).
    bool SendCommand(std::string_view a_jsonLine);

private:
    std::string _pipeName;
    std::atomic<bool> _running{ false };
    std::thread _thread;

    // Handle is owned by the worker thread. Outbound writes lock the handle
    // mutex to avoid racing with the worker swapping it on reconnect.
    std::mutex _handleMtx;
    void* _handle{ nullptr };  // HANDLE, kept void* to avoid <Windows.h> in header

    void Loop();
};

}  // namespace skygraph::viewer
