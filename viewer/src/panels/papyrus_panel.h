#pragma once

#include <string>
#include <unordered_set>

namespace skygraph::viewer {

class TelemetryStore;

namespace panels::papyrus {

// State held by the panel between frames (sort spec, pinned-script set).
struct State {
    int sort_col{ 1 };           // 0=name, 1=us_window, 2=cps, 3=pct_frame
    bool sort_desc{ true };
    std::unordered_set<std::string> pinned;
};

void Draw(const TelemetryStore& a_store, State& a_state);

}  // namespace panels::papyrus

}  // namespace skygraph::viewer
