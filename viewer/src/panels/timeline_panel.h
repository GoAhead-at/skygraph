#pragma once

#include <optional>

namespace skygraph::viewer {

class TelemetryStore;
class NdjsonSource;

namespace panels::timeline {

struct State {
    // When in replay mode, this is the user's chosen "playhead" time. The
    // panels can clamp their displayed data to <= playhead_t to support
    // scrubbing through a recording. Unused in live mode.
    std::optional<double> playhead_t;

    // Latched once the user starts scrubbing; reset on file change.
    bool initialized{ false };
};

// Renders a horizontal mini-overview of the whole session, with markers for
// every event.stutter and a movable playhead in replay mode.
void Draw(const TelemetryStore& a_store,
          const NdjsonSource& a_source,
          State& a_state);

}  // namespace panels::timeline

}  // namespace skygraph::viewer
