#pragma once

#include <cstdint>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace skygraph::samplers {

// Per-script CPU-time attribution map. The hook on the Papyrus VM stack
// run/yield path calls Add(scriptName, microseconds) on every stack
// execution; the PapyrusSampler periodically calls SnapshotTopAndDecay() to
// build the top-N hot-script list and apply an exponential decay to the
// accumulators so old activity fades.
//
// All API is thread-safe behind one mutex. The hook hot path is one map
// lookup + one accumulator update; tested with several thousand script
// executions per second this stays well under 1% CPU.
//
// Unit-testable in isolation: no game APIs are referenced from this file.
class PapyrusAttribution {
public:
    struct Entry {
        std::string name;
        std::uint64_t total_us{ 0 };       // exponentially-decayed window total
        std::uint64_t calls_in_window{ 0 };
    };

    // Hot-path: bump a script's total by some microseconds.
    static void Add(std::string_view a_scriptName, std::uint64_t a_us);

    // Snapshot the top-N entries by total_us (descending). Optionally apply
    // an exponential decay to all entries (factor in (0,1]; 0 = clear,
    // 1 = no decay). Returns the top-N copies for handing to the writer.
    //
    // The combined snapshot+decay is one mutex acquisition; intended to be
    // called from the 10Hz sampler thread.
    static std::vector<Entry> SnapshotTopAndDecay(std::size_t a_topN,
                                                  double a_decay,
                                                  double a_windowSeconds);

    // Drops all attribution. Used by Clear() and on plugin shutdown.
    static void Reset();

    // Total bytes of strings stored (rough health metric).
    static std::size_t SizeForDebug();

private:
    // Lazily-initialized singleton storage.
    struct Storage {
        std::mutex mtx;
        std::unordered_map<std::string, Entry> table;
    };
    static Storage& Get();
};

}  // namespace skygraph::samplers
