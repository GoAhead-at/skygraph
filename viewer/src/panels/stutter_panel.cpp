#include "panels/stutter_panel.h"

#include "state/telemetry_store.h"

#include <imgui.h>

#include <fmt/format.h>

#include <algorithm>
#include <ctime>
#include <vector>

namespace skygraph::viewer::panels::stutter {

namespace {

std::string FormatTime(double a_epochSec) {
    auto secs = static_cast<std::time_t>(a_epochSec);
    std::tm tm{};
    localtime_s(&tm, &secs);
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%02d:%02d:%02d",
                  tm.tm_hour, tm.tm_min, tm.tm_sec);
    return buf;
}

// Find the "guilty" subsystem name for a row -- the largest entry in the
// snapshot's cpu_breakdown. Helps the user immediately see "ah, it was AI".
const char* TopSubsystem(const nlohmann::json& a_snap) {
    if (!a_snap.is_object()) return "?";
    auto it = a_snap.find("cpu_breakdown");
    if (it == a_snap.end() || !it->is_object()) return "?";
    const char* names[] = { "papyrus_ms", "havok_ms", "ai_ms",
                            "render_submit_ms", "streaming_ms", "other_ms" };
    const char* labels[] = { "papyrus", "havok", "ai",
                             "render", "streaming", "other" };
    float best = 0.0f;
    const char* who = "?";
    for (int i = 0; i < 6; ++i) {
        const float v = (*it).value(names[i], 0.0f);
        if (v > best) { best = v; who = labels[i]; }
    }
    return who;
}

void DrawDetails(const TelemetryStore::StutterEntry& a_e) {
    ImGui::Text("Frame: %.2f ms  |  p50: %.2f ms  |  ratio: %.2fx",
                a_e.frame_ms, a_e.p50_ms, a_e.ratio);
    ImGui::Separator();
    if (!a_e.snapshot.is_object()) {
        ImGui::TextDisabled("(no snapshot in record)");
        return;
    }
    if (auto bd = a_e.snapshot.find("cpu_breakdown");
        bd != a_e.snapshot.end() && bd->is_object()) {
        ImGui::Text("CPU breakdown:");
        ImGui::Indent();
        for (auto it = bd->begin(); it != bd->end(); ++it) {
            ImGui::Text("  %-20s %.2f ms", it.key().c_str(),
                        it.value().get<float>());
        }
        ImGui::Unindent();
    }
    ImGui::Separator();
    if (auto cell = a_e.snapshot.find("cell");
        cell != a_e.snapshot.end() && cell->is_string()) {
        ImGui::Text("Cell:               %s",
                    cell->get<std::string>().c_str());
    }
    if (auto ifc = a_e.snapshot.find("in_flight_cell_load");
        ifc != a_e.snapshot.end() && ifc->is_string()
        && !ifc->get<std::string>().empty()) {
        ImGui::Text("In-flight cell:     %s (cell load in progress)",
                    ifc->get<std::string>().c_str());
    }
    if (auto v = a_e.snapshot.find("vram_headroom_mb"); v != a_e.snapshot.end()) {
        ImGui::Text("VRAM headroom:      %.0f MB", v->get<float>());
    }
    if (auto v = a_e.snapshot.find("page_faults_per_sec"); v != a_e.snapshot.end()) {
        ImGui::Text("Page faults/s:      %.0f", v->get<float>());
    }
    if (auto v = a_e.snapshot.find("streaming_queue_depth"); v != a_e.snapshot.end()) {
        ImGui::Text("Streaming queue:    %u", v->get<unsigned>());
    }
    if (auto ac = a_e.snapshot.find("actor_counts");
        ac != a_e.snapshot.end() && ac->is_object()) {
        ImGui::Text("Actors:             hi %u / mh %u / ml %u / lo %u",
                    ac->value("high", 0u),
                    ac->value("mid_high", 0u),
                    ac->value("mid_low", 0u),
                    ac->value("low", 0u));
    }
    if (auto tp = a_e.snapshot.find("top_papyrus");
        tp != a_e.snapshot.end() && tp->is_array() && !tp->empty()) {
        ImGui::Separator();
        ImGui::Text("Top Papyrus scripts at stutter:");
        ImGui::Indent();
        for (const auto& s : *tp) {
            ImGui::Text("  %-32s %llu us",
                        s.value("name", "").c_str(),
                        static_cast<unsigned long long>(s.value("us", 0ULL)));
        }
        ImGui::Unindent();
    }
}

}  // namespace

void Draw(const TelemetryStore& a_store, State& a_state) {
    ImGui::Text("%zu stutters captured", a_store.stutters.size());
    ImGui::Separator();

    constexpr ImGuiTableFlags kFlags =
        ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg
        | ImGuiTableFlags_Sortable | ImGuiTableFlags_ScrollY
        | ImGuiTableFlags_Resizable;

    const float listHeight = ImGui::GetContentRegionAvail().y * 0.55f;

    if (ImGui::BeginTable("stutter_list", 5, kFlags, ImVec2(0, listHeight))) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("time", ImGuiTableColumnFlags_WidthFixed, 60.0f, 0);
        ImGui::TableSetupColumn("frame ms", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_DefaultSort | ImGuiTableColumnFlags_PreferSortDescending, 80.0f, 1);
        ImGui::TableSetupColumn("x p50", ImGuiTableColumnFlags_WidthFixed, 50.0f, 2);
        ImGui::TableSetupColumn("guilty", ImGuiTableColumnFlags_WidthFixed, 80.0f);
        ImGui::TableSetupColumn("cell", 0);
        ImGui::TableHeadersRow();

        std::vector<int> idx;
        idx.reserve(a_store.stutters.size());
        for (int i = 0; i < static_cast<int>(a_store.stutters.size()); ++i) {
            idx.push_back(i);
        }
        if (auto* spec = ImGui::TableGetSortSpecs(); spec && spec->SpecsCount > 0) {
            a_state.sort_col = spec->Specs[0].ColumnUserID;
            a_state.sort_desc = (spec->Specs[0].SortDirection == ImGuiSortDirection_Descending);
        }
        std::sort(idx.begin(), idx.end(), [&](int a, int b) {
            const auto& A = a_store.stutters[a];
            const auto& B = a_store.stutters[b];
            const bool d = a_state.sort_desc;
            auto cmp = [d](auto x, auto y) { return d ? (x > y) : (x < y); };
            switch (a_state.sort_col) {
                case 0: return cmp(A.t, B.t);
                case 2: return cmp(A.ratio, B.ratio);
                case 1:
                default: return cmp(A.frame_ms, B.frame_ms);
            }
        });

        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(idx.size()));
        while (clipper.Step()) {
            for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
                const int i = idx[row];
                const auto& e = a_store.stutters[i];
                ImGui::PushID(i);
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                bool selected = (a_state.selected_index == i);
                if (ImGui::Selectable(FormatTime(e.t).c_str(), selected,
                                      ImGuiSelectableFlags_SpanAllColumns)) {
                    a_state.selected_index = i;
                    a_state.jump_to_t = e.t;
                }
                ImGui::TableSetColumnIndex(1);
                ImGui::Text("%.2f", e.frame_ms);
                ImGui::TableSetColumnIndex(2);
                ImGui::Text("%.2fx", e.ratio);
                ImGui::TableSetColumnIndex(3);
                ImGui::Text("%s", TopSubsystem(e.snapshot));
                ImGui::TableSetColumnIndex(4);
                ImGui::Text("%s",
                    e.snapshot.is_object()
                        ? e.snapshot.value("cell", std::string{}).c_str()
                        : "");
                ImGui::PopID();
            }
        }
        ImGui::EndTable();
    }

    ImGui::Separator();
    if (a_state.selected_index >= 0
        && a_state.selected_index < static_cast<int>(a_store.stutters.size())) {
        DrawDetails(a_store.stutters[a_state.selected_index]);
    } else {
        ImGui::TextDisabled("Select a stutter to see the full snapshot.");
    }
}

}  // namespace skygraph::viewer::panels::stutter
