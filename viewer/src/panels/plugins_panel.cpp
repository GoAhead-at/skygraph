#include "panels/plugins_panel.h"

#include "state/telemetry_store.h"

#include <imgui.h>

#include <cstring>

namespace skygraph::viewer::panels::plugins {

void Draw(const TelemetryStore& a_store, State& a_state) {
    ImGui::Text("%zu plugins", a_store.load_order.size());
    ImGui::SetNextItemWidth(-1);
    ImGui::InputTextWithHint("##plugin_filter", "filter (substring)",
                             a_state.filter, sizeof(a_state.filter));
    ImGui::Separator();

    if (ImGui::BeginChild("##plist", ImVec2(0, 0))) {
        for (std::size_t i = 0; i < a_store.load_order.size(); ++i) {
            const auto& name = a_store.load_order[i];
            if (a_state.filter[0] != 0
                && name.find(a_state.filter) == std::string::npos) {
                continue;
            }
            ImGui::Text("%03zu  %s", i, name.c_str());
        }
    }
    ImGui::EndChild();
}

}  // namespace skygraph::viewer::panels::plugins
