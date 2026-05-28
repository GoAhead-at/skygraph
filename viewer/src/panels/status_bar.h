#pragma once

#include <functional>
#include <string>

namespace skygraph::viewer {

class TelemetryStore;
class NdjsonSource;

// Renders the top status bar: connection pill, FPS / frame time, memory,
// current cell, session timer, total records, drop counters, and a Save
// Session button (only enabled when connected to a live pipe).
namespace panels::status_bar {

struct State {
    // Buffer for the save-session-name modal popup.
    char save_name[64]{};
    bool save_popup_open{ false };
};

struct Callbacks {
    std::function<void(const std::string&)> on_save_session;
    std::function<void()> on_open_replay;
    std::function<void()> on_connect_live;
    std::function<void()> on_exit;
};

void Draw(const TelemetryStore& a_store,
          const NdjsonSource& a_source,
          State& a_state,
          const Callbacks& a_cb);

}  // namespace panels::status_bar

}  // namespace skygraph::viewer
