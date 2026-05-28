#pragma once

namespace skygraph::protocol {

// Duplex named pipe. Plugin opens this as a server (one instance per concurrent
// viewer); viewers open it as clients.
inline constexpr const char* kPipeName = R"(\\.\pipe\skygraph)";

// Default outbound buffer size hint for CreateNamedPipe; large enough to hold
// a few stutter snapshots without backpressure stalls.
inline constexpr unsigned kPipeBufferBytes = 1 << 20;  // 1 MiB

// Max concurrent viewer connections.
inline constexpr unsigned kPipeMaxInstances = 4;

}  // namespace skygraph::protocol
