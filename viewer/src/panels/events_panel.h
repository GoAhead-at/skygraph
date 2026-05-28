#pragma once

#include <string>

namespace skygraph::viewer {

class TelemetryStore;

namespace panels::events {

struct State {
    char filter[64]{};
    bool show_cell{ true };
    bool show_save{ true };
    bool show_mod_event{ true };
    bool show_streaming_hitch{ true };
    bool show_crash{ true };
    bool auto_scroll{ true };
};

void Draw(const TelemetryStore& a_store, State& a_state);

}  // namespace panels::events

}  // namespace skygraph::viewer
