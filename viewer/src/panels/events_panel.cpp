#include "panels/events_panel.h"

#include "state/telemetry_store.h"

#include <imgui.h>

#include <fmt/format.h>

#include <ctime>

namespace skygraph::viewer::panels::events {

namespace {

ImVec4 ColorFor(std::string_view a_type) {
    if (a_type == "event.crash") return ImVec4(0.95f, 0.30f, 0.30f, 1.0f);
    if (a_type == "event.streaming_hitch") return ImVec4(1.00f, 0.65f, 0.20f, 1.0f);
    if (a_type == "event.stutter") return ImVec4(1.00f, 0.50f, 0.50f, 1.0f);
    if (a_type == "event.cell_attach") return ImVec4(0.55f, 0.85f, 1.00f, 1.0f);
    if (a_type == "event.cell_detach") return ImVec4(0.45f, 0.65f, 0.85f, 1.0f);
    if (a_type == "event.save" || a_type == "event.autosave")
        return ImVec4(0.55f, 0.95f, 0.55f, 1.0f);
    return ImVec4(0.80f, 0.80f, 0.80f, 1.0f);
}

bool PassesFilter(const TelemetryStore::EventEntry& a_e, const State& a_s) {
    if (!a_s.show_cell && a_e.type.starts_with("event.cell_")) return false;
    if (!a_s.show_save && (a_e.type == "event.save" || a_e.type == "event.autosave")) return false;
    if (!a_s.show_mod_event && a_e.type == "event.mod_event") return false;
    if (!a_s.show_streaming_hitch && a_e.type == "event.streaming_hitch") return false;
    if (!a_s.show_crash && a_e.type == "event.crash") return false;
    if (a_s.filter[0] != 0) {
        if (a_e.summary.find(a_s.filter) == std::string::npos
            && a_e.type.find(a_s.filter) == std::string::npos) {
            return false;
        }
    }
    return true;
}

std::string FormatTime(double a_epochSec) {
    auto secs = static_cast<std::time_t>(a_epochSec);
    std::tm tm{};
    localtime_s(&tm, &secs);
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%02d:%02d:%02d",
                  tm.tm_hour, tm.tm_min, tm.tm_sec);
    return buf;
}

}  // namespace

void Draw(const TelemetryStore& a_store, State& a_state) {
    ImGui::Checkbox("cell", &a_state.show_cell); ImGui::SameLine();
    ImGui::Checkbox("save", &a_state.show_save); ImGui::SameLine();
    ImGui::Checkbox("mod_event", &a_state.show_mod_event); ImGui::SameLine();
    ImGui::Checkbox("stream_hitch", &a_state.show_streaming_hitch); ImGui::SameLine();
    ImGui::Checkbox("crash", &a_state.show_crash); ImGui::SameLine();
    ImGui::Checkbox("auto-scroll", &a_state.auto_scroll);
    ImGui::SetNextItemWidth(-1);
    ImGui::InputTextWithHint("##filter", "filter (substring)",
                             a_state.filter, sizeof(a_state.filter));
    ImGui::Separator();

    if (ImGui::BeginChild("##eventlist", ImVec2(0, 0))) {
        for (const auto& e : a_store.events) {
            if (!PassesFilter(e, a_state)) continue;
            const auto col = ColorFor(e.type);
            ImGui::TextColored(ImVec4(0.55f, 0.55f, 0.55f, 1.0f),
                               "%s", FormatTime(e.t).c_str());
            ImGui::SameLine();
            ImGui::TextColored(col, "%s", e.type.c_str());
            ImGui::SameLine();
            ImGui::TextUnformatted(e.summary.c_str());
        }
        if (a_state.auto_scroll
            && ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 50.0f) {
            ImGui::SetScrollHereY(1.0f);
        }
    }
    ImGui::EndChild();
}

}  // namespace skygraph::viewer::panels::events
