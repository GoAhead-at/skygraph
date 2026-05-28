#pragma once

namespace skygraph::viewer {

class TelemetryStore;

namespace panels::plugins {

struct State {
    char filter[64]{};
};

void Draw(const TelemetryStore& a_store, State& a_state);

}  // namespace panels::plugins

}  // namespace skygraph::viewer
