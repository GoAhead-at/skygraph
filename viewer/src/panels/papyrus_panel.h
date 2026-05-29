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
    // Pie source: true = cumulative-since-connect (default), false = live
    // (the fast-decaying snapshot used by the table).
    bool pie_cumulative{ true };
};

// Non-const store: the panel's "Reset" button clears the cumulative pie
// accumulator held in the store.
void Draw(TelemetryStore& a_store, State& a_state);

}  // namespace panels::papyrus

}  // namespace skygraph::viewer
