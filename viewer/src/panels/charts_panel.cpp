#include "panels/charts_panel.h"

#include "state/telemetry_store.h"

#include <imgui.h>
#include <implot.h>

#include <vector>

namespace skygraph::viewer::panels::charts {

namespace {

// Pull a series out of a deque<T> with member accessors. We materialize into
// thin scratch vectors per draw -- the deques cap at ~3600 entries so the
// allocator pressure is negligible at 60fps UI.
template <typename Deque, typename Accessor>
void Extract(const Deque& a_q, std::vector<double>& a_xs,
             std::vector<double>& a_ys, Accessor a_acc) {
    a_xs.clear();
    a_ys.clear();
    a_xs.reserve(a_q.size());
    a_ys.reserve(a_q.size());
    for (const auto& s : a_q) {
        a_xs.push_back(s.t);
        a_ys.push_back(static_cast<double>(a_acc(s)));
    }
}

void DrawFrameChart(const TelemetryStore& a_store,
                    std::vector<double>& a_xs,
                    std::vector<double>& a_ys1,
                    std::vector<double>& a_ys2,
                    std::vector<double>& a_ys3) {
    if (a_store.frames.empty()) {
        ImGui::TextDisabled("waiting for frame data...");
        return;
    }
    // X range = last 60 seconds.
    const double tMax = a_store.frames.back().t;
    const double tMin = tMax - 60.0;

    if (ImPlot::BeginPlot("##frame", ImVec2(-1, 200), ImPlotFlags_NoTitle)) {
        ImPlot::SetupAxes("time (s)", "ms",
                          ImPlotAxisFlags_NoTickLabels,
                          ImPlotAxisFlags_AutoFit);
        ImPlot::SetupAxisLimits(ImAxis_X1, tMin, tMax, ImGuiCond_Always);

        Extract(a_store.frames, a_xs, a_ys1,
                [](const auto& s) { return s.dt_ms; });
        ImPlot::PlotLine("dt", a_xs.data(), a_ys1.data(),
                         static_cast<int>(a_xs.size()));

        Extract(a_store.frames, a_xs, a_ys2,
                [](const auto& s) { return s.cpu_ms; });
        ImPlot::PlotLine("cpu", a_xs.data(), a_ys2.data(),
                         static_cast<int>(a_xs.size()));

        Extract(a_store.frames, a_xs, a_ys3,
                [](const auto& s) { return s.gpu_ms; });
        ImPlot::PlotLine("gpu", a_xs.data(), a_ys3.data(),
                         static_cast<int>(a_xs.size()));

        // 16.67ms reference line.
        double ref[2] = { 16.667, 16.667 };
        double refX[2] = { tMin, tMax };
        ImPlot::PushStyleColor(ImPlotCol_Line, ImVec4(0.5f, 0.5f, 0.5f, 0.6f));
        ImPlot::PlotLine("60fps", refX, ref, 2);
        ImPlot::PopStyleColor();

        ImPlot::EndPlot();
    }
}

void DrawMemoryChart(const TelemetryStore& a_store,
                     std::vector<double>& a_xs,
                     std::vector<double>& a_ys1,
                     std::vector<double>& a_ys2,
                     std::vector<double>& a_ys3) {
    if (a_store.memories.empty()) {
        ImGui::TextDisabled("waiting for memory data...");
        return;
    }
    const double tMax = a_store.memories.back().t;
    const double tMin = tMax - 600.0;

    if (ImPlot::BeginPlot("##mem", ImVec2(-1, 180), ImPlotFlags_NoTitle)) {
        ImPlot::SetupAxes("time (s)", "MB",
                          ImPlotAxisFlags_NoTickLabels,
                          ImPlotAxisFlags_AutoFit);
        ImPlot::SetupAxisLimits(ImAxis_X1, tMin, tMax, ImGuiCond_Always);

        Extract(a_store.memories, a_xs, a_ys1,
                [](const auto& s) { return s.working_set_mb; });
        ImPlot::PlotLine("working set", a_xs.data(), a_ys1.data(),
                         static_cast<int>(a_xs.size()));

        Extract(a_store.memories, a_xs, a_ys2,
                [](const auto& s) { return s.vram_used_mb; });
        ImPlot::PlotLine("vram used", a_xs.data(), a_ys2.data(),
                         static_cast<int>(a_xs.size()));

        Extract(a_store.memories, a_xs, a_ys3,
                [](const auto& s) { return s.vram_budget_mb; });
        ImPlot::PushStyleVar(ImPlotStyleVar_LineWeight, 1.0f);
        ImPlot::PushStyleColor(ImPlotCol_Line, ImVec4(0.5f, 0.5f, 0.5f, 0.5f));
        ImPlot::PlotLine("vram budget", a_xs.data(), a_ys3.data(),
                         static_cast<int>(a_xs.size()));
        ImPlot::PopStyleColor();
        ImPlot::PopStyleVar();

        ImPlot::EndPlot();
    }
}

void DrawPressureChart(const TelemetryStore& a_store,
                       std::vector<double>& a_xs,
                       std::vector<double>& a_ys) {
    if (a_store.pressures.empty()) {
        ImGui::TextDisabled("waiting for pressure data...");
        return;
    }
    const double tMax = a_store.pressures.back().t;
    const double tMin = tMax - 600.0;

    if (ImPlot::BeginPlot("##pressure", ImVec2(-1, 140), ImPlotFlags_NoTitle)) {
        ImPlot::SetupAxes("time (s)", "faults/s",
                          ImPlotAxisFlags_NoTickLabels,
                          ImPlotAxisFlags_AutoFit);
        ImPlot::SetupAxisLimits(ImAxis_X1, tMin, tMax, ImGuiCond_Always);

        Extract(a_store.pressures, a_xs, a_ys,
                [](const auto& s) { return s.page_faults_per_sec; });
        ImPlot::PlotLine("page faults", a_xs.data(), a_ys.data(),
                         static_cast<int>(a_xs.size()));

        ImPlot::EndPlot();
    }
}

}  // namespace

namespace {

// Compute rolling p99 (1% low) and p99.9 (0.1% low) and the mean over the
// frames window. Cheap because the window is bounded (~3600 entries max).
struct PacingStats {
    double mean_fps{ 0.0 };
    double p99_low_fps{ 0.0 };   // 99th percentile worst frame -> FPS
    double p999_low_fps{ 0.0 };
};
PacingStats ComputePacing(const TelemetryStore& a_store) {
    PacingStats out;
    if (a_store.frames.empty()) return out;
    std::vector<float> dt;
    dt.reserve(a_store.frames.size());
    double sum = 0.0;
    for (const auto& f : a_store.frames) {
        if (f.dt_ms > 0.0f) {
            dt.push_back(f.dt_ms);
            sum += f.dt_ms;
        }
    }
    if (dt.empty()) return out;

    std::sort(dt.begin(), dt.end());  // ascending; large frame_ms = bad
    out.mean_fps = 1000.0 / (sum / dt.size());

    auto pIdx = [&](double a_q) {
        auto i = static_cast<std::size_t>(a_q * (dt.size() - 1));
        return i < dt.size() ? i : dt.size() - 1;
    };
    out.p99_low_fps = 1000.0 / dt[pIdx(0.99)];
    out.p999_low_fps = 1000.0 / dt[pIdx(0.999)];
    return out;
}

void DrawHistogram(const TelemetryStore& a_store, std::vector<double>& a_scratch) {
    if (a_store.frames.empty()) {
        ImGui::TextDisabled("waiting for frames...");
        return;
    }
    a_scratch.clear();
    a_scratch.reserve(a_store.frames.size());
    for (const auto& f : a_store.frames) a_scratch.push_back(f.dt_ms);

    if (ImPlot::BeginPlot("##frame_hist", ImVec2(-1, 160), ImPlotFlags_NoTitle)) {
        ImPlot::SetupAxes("frame ms", "count",
                          ImPlotAxisFlags_AutoFit, ImPlotAxisFlags_AutoFit);
        ImPlot::PlotHistogram("dt_ms", a_scratch.data(),
                              static_cast<int>(a_scratch.size()),
                              ImPlotBin_Sturges);
        // Reference line at 16.67ms (60fps target)
        double ref[2] = { 0, 0 };
        ImPlot::PushStyleColor(ImPlotCol_InlayText, ImVec4(0.85f, 0.85f, 0.3f, 1.0f));
        (void)ref;
        ImPlot::PopStyleColor();
        ImPlot::EndPlot();
    }
}

void DrawBreakdownChart(const TelemetryStore& a_store,
                        std::vector<double>& a_xs,
                        std::vector<double>& a_papyrus,
                        std::vector<double>& a_havok,
                        std::vector<double>& a_ai,
                        std::vector<double>& a_render,
                        std::vector<double>& a_streaming,
                        std::vector<double>& a_other) {
    if (a_store.breakdowns.empty()) {
        ImGui::TextDisabled("waiting for breakdown data...");
        return;
    }
    const double tMax = a_store.breakdowns.back().t;
    const double tMin = tMax - 60.0;

    // Stacked: compute cumulative ys arrays so PlotShaded between adjacent
    // cumulative lines renders each subsystem's slice on top of the previous.
    a_xs.clear();
    a_papyrus.clear(); a_havok.clear(); a_ai.clear();
    a_render.clear(); a_streaming.clear(); a_other.clear();

    for (const auto& s : a_store.breakdowns) {
        a_xs.push_back(s.t);
        double cum = s.papyrus_ms;
        a_papyrus.push_back(cum);
        cum += s.havok_ms;       a_havok.push_back(cum);
        cum += s.ai_ms;          a_ai.push_back(cum);
        cum += s.render_submit_ms; a_render.push_back(cum);
        cum += s.streaming_ms;   a_streaming.push_back(cum);
        cum += s.other_ms;       a_other.push_back(cum);
    }
    const int n = static_cast<int>(a_xs.size());
    std::vector<double> zero(n, 0.0);

    if (ImPlot::BeginPlot("##breakdown", ImVec2(-1, 220), ImPlotFlags_NoTitle)) {
        ImPlot::SetupAxes("time (s)", "ms",
                          ImPlotAxisFlags_NoTickLabels,
                          ImPlotAxisFlags_AutoFit);
        ImPlot::SetupAxisLimits(ImAxis_X1, tMin, tMax, ImGuiCond_Always);

        // Bottom-up shaded bands. Each band is between the previous cumulative
        // (or zero for papyrus) and the current cumulative.
        auto shade = [&](const char* a_label,
                         const std::vector<double>& a_low,
                         const std::vector<double>& a_high) {
            ImPlot::PlotShaded(a_label, a_xs.data(),
                               a_low.data(), a_high.data(), n);
        };
        shade("papyrus", zero, a_papyrus);
        shade("havok", a_papyrus, a_havok);
        shade("ai", a_havok, a_ai);
        shade("render submit", a_ai, a_render);
        shade("streaming", a_render, a_streaming);
        shade("other", a_streaming, a_other);

        // Top line = total ms.
        ImPlot::PushStyleVar(ImPlotStyleVar_LineWeight, 1.5f);
        ImPlot::PlotLine("total cpu", a_xs.data(), a_other.data(), n);
        ImPlot::PopStyleVar();

        ImPlot::EndPlot();
    }
}

}  // namespace

void Draw(const TelemetryStore& a_store) {
    static std::vector<double> xs, ys1, ys2, ys3;
    static std::vector<double> bd_papyrus, bd_havok, bd_ai, bd_render, bd_stream, bd_other;
    static std::vector<double> hist;

    const auto pacing = ComputePacing(a_store);
    ImGui::Text("Pacing: %.1f mean  |  1%% low %.1f  |  0.1%% low %.1f",
                pacing.mean_fps, pacing.p99_low_fps, pacing.p999_low_fps);

    ImGui::Separator();
    ImGui::TextUnformatted("Frame time");
    DrawFrameChart(a_store, xs, ys1, ys2, ys3);

    ImGui::Separator();
    ImGui::TextUnformatted("Frame-time histogram (60s window)");
    DrawHistogram(a_store, hist);

    ImGui::Separator();
    ImGui::TextUnformatted("CPU breakdown (stacked)");
    DrawBreakdownChart(a_store, xs,
                       bd_papyrus, bd_havok, bd_ai, bd_render, bd_stream, bd_other);

    ImGui::Separator();
    ImGui::TextUnformatted("Memory");
    DrawMemoryChart(a_store, xs, ys1, ys2, ys3);

    ImGui::Separator();
    ImGui::TextUnformatted("Page-fault pressure");
    DrawPressureChart(a_store, xs, ys1);
}

}  // namespace skygraph::viewer::panels::charts
