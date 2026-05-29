#include "panels/papyrus_panel.h"

#include "state/telemetry_store.h"

#include <imgui.h>
#include <implot.h>

#include <fmt/format.h>

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

namespace skygraph::viewer::panels::papyrus {

namespace {

void DrawHeader(const TelemetryStore& a_store) {
    if (a_store.last_papyrus) {
        ImGui::TextUnformatted(
            fmt::format("VM: active {}  suspended {}  latent queue {}",
                        a_store.last_papyrus->active,
                        a_store.last_papyrus->suspended,
                        a_store.last_papyrus->latent).c_str());
    } else {
        ImGui::TextDisabled("waiting for papyrus.snapshot...");
    }
    ImGui::Separator();
}

// Top-N pie chart of script CPU time over the latest snapshot window.
// Anything below the cut-off is folded into a single "(other)" slice so the
// pie always sums to 100% of observed Papyrus work.
void DrawTopPie(const std::vector<TelemetryStore::HotScript>& a_rows) {
    if (a_rows.empty()) return;

    constexpr int kMaxSlices = 10;

    std::vector<TelemetryStore::HotScript> sorted = a_rows;
    const auto cutEnd = sorted.begin() +
        static_cast<std::ptrdiff_t>(
            std::min<std::size_t>(kMaxSlices, sorted.size()));
    std::partial_sort(sorted.begin(), cutEnd, sorted.end(),
                      [](const auto& a, const auto& b) {
        return a.us_window > b.us_window;
    });

    const int topCount = static_cast<int>(cutEnd - sorted.begin());

    std::vector<std::string> labelStore;
    std::vector<const char*> labels;
    std::vector<double> values;
    labelStore.reserve(static_cast<std::size_t>(topCount) + 1);
    labels.reserve(static_cast<std::size_t>(topCount) + 1);
    values.reserve(static_cast<std::size_t>(topCount) + 1);

    for (int i = 0; i < topCount; ++i) {
        if (sorted[i].us_window == 0) continue;
        labelStore.push_back(sorted[i].name);
        values.push_back(static_cast<double>(sorted[i].us_window));
    }

    std::uint64_t otherUs = 0;
    for (std::size_t i = static_cast<std::size_t>(topCount);
         i < sorted.size(); ++i) {
        otherUs += sorted[i].us_window;
    }
    if (otherUs > 0) {
        labelStore.emplace_back("(other)");
        values.push_back(static_cast<double>(otherUs));
    }

    if (values.empty()) return;

    // c_str() pointers must be re-collected AFTER labelStore is final because
    // any prior push_back may have reallocated and invalidated earlier ones.
    for (const auto& s : labelStore) labels.push_back(s.c_str());

    double total = 0.0;
    for (double v : values) total += v;
    if (total <= 0.0) return;

    // ImPlot::PlotPieChart applies its label format string to the RAW value we
    // pass, not to a computed share -- so handing it microseconds prints
    // "2119.0%". Feed it percentages instead; the wedge angles are unaffected
    // (ImPlot normalizes by the sum either way) and the labels read correctly.
    std::vector<double> percents;
    percents.reserve(values.size());
    for (double v : values) percents.push_back(v / total * 100.0);

    // The pie gets a FIXED square plot region and NO ImPlot legend. An
    // outside legend would reserve a variable amount of width (it grows with
    // the longest script name), which shrinks the leftover plot rect and
    // makes the circle resize whenever the script names change. By fixing the
    // plot to kPieSize x kPieSize and rendering our own legend beside it, the
    // pie stays exactly the same size no matter how long the names are.
    constexpr float kPieSize = 220.0f;

    ImGui::BeginGroup();
    if (ImPlot::BeginPlot("##script_pie", ImVec2(kPieSize, kPieSize),
                          ImPlotFlags_Equal | ImPlotFlags_NoMouseText
                          | ImPlotFlags_NoLegend)) {
        ImPlot::SetupAxes(nullptr, nullptr,
                          ImPlotAxisFlags_NoDecorations,
                          ImPlotAxisFlags_NoDecorations);
        ImPlot::SetupAxesLimits(0, 1, 0, 1, ImPlotCond_Always);
        ImPlot::PlotPieChart(labels.data(), percents.data(),
                             static_cast<int>(percents.size()),
                             0.5, 0.5, 0.4, "%.1f%%", 90.0,
                             ImPlotPieChartFlags_Normalize);
        ImPlot::EndPlot();
    }
    ImGui::EndGroup();

    // Manual legend to the right. Free to be as wide as the names require
    // without ever feeding back into the pie's size. Swatch colors are pulled
    // from the same colormap PlotPieChart used (slice i -> colormap index i).
    ImGui::SameLine();
    ImGui::BeginGroup();
    for (std::size_t i = 0; i < labelStore.size(); ++i) {
        const ImVec4 col =
            ImPlot::GetColormapColor(static_cast<int>(i));
        const double pct = values[i] / total * 100.0;
        ImGui::ColorButton(fmt::format("##sw{}", i).c_str(), col,
                           ImGuiColorEditFlags_NoTooltip
                           | ImGuiColorEditFlags_NoDragDrop,
                           ImVec2(12, 12));
        ImGui::SameLine();
        ImGui::TextUnformatted(
            fmt::format("{:5.1f}%  {}", pct, labelStore[i]).c_str());
    }
    ImGui::EndGroup();

    ImGui::Separator();
}

}  // namespace

void Draw(TelemetryStore& a_store, State& a_state) {
    DrawHeader(a_store);

    if (a_store.hot_scripts.empty() && a_store.papyrus_cumulative_us.empty()) {
        ImGui::TextDisabled("no script timing yet (VM hook may not be installed)");
        return;
    }

    // Pie source selector. Cumulative = total activity since connect/reset
    // (stable, answers "what's been busiest"); Live = the decayed snapshot
    // (volatile, answers "what's hot this instant", same data as the table).
    ImGui::TextUnformatted("Pie:");
    ImGui::SameLine();
    if (ImGui::RadioButton("Cumulative", a_state.pie_cumulative)) {
        a_state.pie_cumulative = true;
    }
    ImGui::SameLine();
    if (ImGui::RadioButton("Live", !a_state.pie_cumulative)) {
        a_state.pie_cumulative = false;
    }
    if (a_state.pie_cumulative) {
        ImGui::SameLine();
        if (ImGui::SmallButton("Reset")) {
            a_store.ResetPapyrusCumulative();
        }
    }

    if (a_state.pie_cumulative) {
        std::vector<TelemetryStore::HotScript> cumulative;
        cumulative.reserve(a_store.papyrus_cumulative_us.size());
        for (const auto& [name, us] : a_store.papyrus_cumulative_us) {
            cumulative.push_back(TelemetryStore::HotScript{
                name, static_cast<std::uint64_t>(us), 0.0f, 0.0f });
        }
        DrawTopPie(cumulative);
    } else {
        DrawTopPie(a_store.hot_scripts);
    }

    constexpr ImGuiTableFlags kFlags =
        ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg
        | ImGuiTableFlags_ScrollY | ImGuiTableFlags_Sortable
        | ImGuiTableFlags_SortMulti | ImGuiTableFlags_Resizable
        | ImGuiTableFlags_Reorderable | ImGuiTableFlags_Hideable;

    if (!ImGui::BeginTable("hot_scripts", 5, kFlags)) return;

    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableSetupColumn("Pin", ImGuiTableColumnFlags_NoSort | ImGuiTableColumnFlags_WidthFixed, 28.0f);
    ImGui::TableSetupColumn("Script", ImGuiTableColumnFlags_DefaultSort, 0.0f, 0);
    ImGui::TableSetupColumn("us / window", ImGuiTableColumnFlags_DefaultSort | ImGuiTableColumnFlags_PreferSortDescending, 0.0f, 1);
    ImGui::TableSetupColumn("calls/s", 0, 0.0f, 2);
    ImGui::TableSetupColumn("% frame", 0, 0.0f, 3);
    ImGui::TableHeadersRow();

    // Copy + sort.
    std::vector<TelemetryStore::HotScript> rows = a_store.hot_scripts;

    if (auto* spec = ImGui::TableGetSortSpecs(); spec && spec->SpecsCount > 0) {
        const auto& s = spec->Specs[0];
        a_state.sort_col = s.ColumnUserID;
        a_state.sort_desc = (s.SortDirection == ImGuiSortDirection_Descending);
    }

    std::sort(rows.begin(), rows.end(),
              [&](const auto& a, const auto& b) {
        const bool desc = a_state.sort_desc;
        auto cmp = [desc](auto x, auto y) {
            return desc ? (x > y) : (x < y);
        };
        switch (a_state.sort_col) {
            case 0: return cmp(a.name, b.name);
            case 2: return cmp(a.calls_per_sec, b.calls_per_sec);
            case 3: return cmp(a.pct_frame, b.pct_frame);
            case 1:
            default: return cmp(a.us_window, b.us_window);
        }
    });

    // Render pinned rows first.
    auto renderRow = [&](const TelemetryStore::HotScript& r) {
        ImGui::TableNextRow();
        const bool pinned = a_state.pinned.contains(r.name);

        ImGui::TableSetColumnIndex(0);
        ImGui::PushID(r.name.c_str());
        if (ImGui::Checkbox("##pin", const_cast<bool*>(&pinned))) {
            if (a_state.pinned.contains(r.name)) {
                a_state.pinned.erase(r.name);
            } else {
                a_state.pinned.insert(r.name);
            }
        }
        ImGui::PopID();

        ImGui::TableSetColumnIndex(1);
        if (pinned) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.85f, 0.4f, 1.0f));
        }
        ImGui::TextUnformatted(r.name.c_str());
        if (pinned) ImGui::PopStyleColor();

        ImGui::TableSetColumnIndex(2);
        ImGui::TextUnformatted(fmt::format("{}", r.us_window).c_str());
        ImGui::TableSetColumnIndex(3);
        ImGui::TextUnformatted(fmt::format("{:.1f}", r.calls_per_sec).c_str());
        ImGui::TableSetColumnIndex(4);
        ImGui::TextUnformatted(fmt::format("{:.2f}%", r.pct_frame * 100.0f).c_str());
    };

    for (const auto& r : rows) {
        if (a_state.pinned.contains(r.name)) renderRow(r);
    }
    for (const auto& r : rows) {
        if (!a_state.pinned.contains(r.name)) renderRow(r);
    }
    ImGui::EndTable();
}

}  // namespace skygraph::viewer::panels::papyrus
