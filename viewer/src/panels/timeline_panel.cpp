#include "panels/timeline_panel.h"

#include "state/telemetry_store.h"
#include "transport/ndjson_source.h"

#include <imgui.h>
#include <implot.h>

#include <algorithm>
#include <vector>

namespace skygraph::viewer::panels::timeline {

void Draw(const TelemetryStore& a_store,
          const NdjsonSource& a_source,
          State& a_state) {
    if (a_store.frames.empty()) {
        ImGui::TextDisabled("Timeline activates once frame data starts arriving.");
        return;
    }

    const double tMin = a_store.frames.front().t;
    const double tMax = a_store.frames.back().t;
    const bool replay = (a_source.CurrentState() == NdjsonSource::State::Replaying);

    if (replay && !a_state.initialized) {
        a_state.playhead_t = tMax;
        a_state.initialized = true;
    }

    ImGui::Text("Session: %.1f s   |   %s   |   %zu stutters",
                tMax - tMin,
                replay ? "REPLAY" : "LIVE",
                a_store.stutters.size());

    if (replay && a_state.playhead_t) {
        double ph = *a_state.playhead_t;
        if (ImGui::SliderScalar("Playhead", ImGuiDataType_Double,
                                &ph, &tMin, &tMax, "%.2f")) {
            a_state.playhead_t = ph;
        }
        ImGui::SameLine();
        if (ImGui::Button("Live")) {
            a_state.playhead_t = tMax;
        }
    }

    // Mini overview chart with stutter markers.
    if (ImPlot::BeginPlot("##overview", ImVec2(-1, 80),
                          ImPlotFlags_NoTitle | ImPlotFlags_NoMenus
                              | ImPlotFlags_NoBoxSelect)) {
        ImPlot::SetupAxes(nullptr, nullptr,
                          ImPlotAxisFlags_NoTickLabels | ImPlotAxisFlags_NoMenus
                              | ImPlotAxisFlags_NoSideSwitch,
                          ImPlotAxisFlags_AutoFit);
        ImPlot::SetupAxisLimits(ImAxis_X1, tMin, tMax, ImGuiCond_Always);

        // Stutter ticks at the bottom.
        std::vector<double> sx;
        sx.reserve(a_store.stutters.size());
        for (const auto& s : a_store.stutters) sx.push_back(s.t);
        if (!sx.empty()) {
            ImPlot::PlotInfLines("stutters", sx.data(),
                                 static_cast<int>(sx.size()));
        }
        // Playhead line.
        if (a_state.playhead_t) {
            double ph = *a_state.playhead_t;
            ImPlot::PushStyleColor(ImPlotCol_Line,
                                   ImVec4(0.9f, 0.9f, 0.3f, 1.0f));
            ImPlot::PlotInfLines("playhead", &ph, 1);
            ImPlot::PopStyleColor();
        }
        ImPlot::EndPlot();
    }
}

}  // namespace skygraph::viewer::panels::timeline
