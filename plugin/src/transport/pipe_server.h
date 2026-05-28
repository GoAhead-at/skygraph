#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace skygraph::transport {

// One server thread, multiple client instances. The plugin pushes serialized
// NDJSON records via Broadcast(); each connected client receives a copy.
//
// Commands flowing the other direction (viewer -> plugin) are handed off to
// the OnCommand callback, which runs on a reader thread per client.
class PipeServer {
public:
    using CommandHandler = std::function<void(std::string_view a_jsonLine)>;

    PipeServer(std::string a_pipeName,
               unsigned a_maxInstances,
               unsigned a_bufferBytes,
               CommandHandler a_onCommand);
    ~PipeServer();

    PipeServer(const PipeServer&) = delete;
    PipeServer& operator=(const PipeServer&) = delete;

    void Start();
    void Stop();

    // Fan a record (with trailing \n already baked in by the caller's
    // serializer) out to every connected client. Non-blocking; clients with
    // backed-up buffers may drop records (counted internally).
    void Broadcast(std::string_view a_recordWithNewline);

    unsigned ConnectedClients() const noexcept {
        return _connectedCount.load(std::memory_order_relaxed);
    }

    std::size_t DroppedBroadcasts() const noexcept {
        return _droppedBroadcasts.load(std::memory_order_relaxed);
    }

private:
    struct Impl;
    std::unique_ptr<Impl> _impl;

    std::string _pipeName;
    unsigned _maxInstances;
    unsigned _bufferBytes;
    CommandHandler _onCommand;
    std::atomic<unsigned> _connectedCount{ 0 };
    std::atomic<std::size_t> _droppedBroadcasts{ 0 };
};

}  // namespace skygraph::transport
