#pragma once

#include <optional>

namespace skygraph::viewer {

class TelemetryStore;

namespace panels::stutter {

struct State {
    int selected_index{ -1};  // selected stutter row, or -1
    int sort_col{ 1 };        // 0=time, 1=frame_ms, 2=ratio
    bool sort_desc{ true };

    // When the user clicks a row this is set to the stutter's timestamp; the
    // charts panel can read it to scroll-into-view (added when timeline lands).
    std::optional<double> jump_to_t;
};

void Draw(const TelemetryStore& a_store, State& a_state);

}  // namespace panels::stutter

}  // namespace skygraph::viewer
