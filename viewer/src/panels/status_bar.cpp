#include "panels/status_bar.h"

#include "state/telemetry_store.h"
#include "transport/ndjson_source.h"

#include <imgui.h>

#include <fmt/format.h>

#include <chrono>

namespace skygraph::viewer::panels::status_bar {

namespace {

ImVec4 StateColor(NdjsonSource::State s) {
    switch (s) {
        case NdjsonSource::State::Connected: return ImVec4(0.30f, 0.85f, 0.35f, 1.0f);
        case NdjsonSource::State::Replaying: return ImVec4(0.50f, 0.65f, 1.00f, 1.0f);
        case NdjsonSource::State::Connecting: return ImVec4(0.95f, 0.80f, 0.25f, 1.0f);
        case NdjsonSource::State::Error: return ImVec4(0.95f, 0.30f, 0.30f, 1.0f);
        case NdjsonSource::State::Disconnected:
        default: return ImVec4(0.55f, 0.55f, 0.55f, 1.0f);
    }
}

const char* StateLabel(NdjsonSource::State s) {
    switch (s) {
        case NdjsonSource::State::Connected: return "Connected";
        case NdjsonSource::State::Replaying: return "Replaying";
        case NdjsonSource::State::Connecting: return "Connecting...";
        case NdjsonSource::State::Error: return "Error";
        case NdjsonSource::State::Disconnected:
        default: return "Disconnected";
    }
}

void Pill(const char* a_label, ImVec4 a_color) {
    ImGui::PushStyleColor(ImGuiCol_Button, a_color);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, a_color);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, a_color);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 9.0f);
    ImGui::Button(a_label);
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(3);
}

}  // namespace

void Draw(const TelemetryStore& a_store,
          const NdjsonSource& a_source,
          State& a_state,
          const Callbacks& a_cb) {
    if (ImGui::BeginMainMenuBar()) {
        if (ImGui::BeginMenu("File")) {
            if (a_cb.on_open_replay && ImGui::MenuItem("Open Session...", "Ctrl+O")) {
                a_cb.on_open_replay();
            }
            if (a_cb.on_connect_live && ImGui::MenuItem("Connect Live Pipe", "Ctrl+L")) {
                a_cb.on_connect_live();
            }
            ImGui::Separator();
            if (a_cb.on_exit && ImGui::MenuItem("Exit", "Alt+F4")) {
                a_cb.on_exit();
            }
            ImGui::EndMenu();
        }

        const auto st = a_source.CurrentState();
        Pill(StateLabel(st), StateColor(st));
        ImGui::Separator();

        if (a_store.last_frame) {
            ImGui::TextUnformatted(
                fmt::format("FPS {:5.1f}  dt {:5.2f} ms  cpu {:5.2f}  gpu {:5.2f}",
                            a_store.last_frame->fps,
                            a_store.last_frame->dt_ms,
                            a_store.last_frame->cpu_ms,
                            a_store.last_frame->gpu_ms).c_str());
        } else {
            ImGui::TextDisabled("FPS  --");
        }
        ImGui::Separator();

        if (a_store.last_memory) {
            ImGui::TextUnformatted(
                fmt::format("Mem {:5.0f} MB  VRAM {:5.0f}/{:5.0f} MB",
                            a_store.last_memory->working_set_mb,
                            a_store.last_memory->vram_used_mb,
                            a_store.last_memory->vram_budget_mb).c_str());
            ImGui::Separator();
        }

        if (a_store.last_papyrus) {
            ImGui::TextUnformatted(
                fmt::format("VM {} active / {} susp / {} latent",
                            a_store.last_papyrus->active,
                            a_store.last_papyrus->suspended,
                            a_store.last_papyrus->latent).c_str());
            ImGui::Separator();
        }

        if (a_store.last_state && !a_store.last_state->cell.empty()) {
            ImGui::TextUnformatted(
                fmt::format("Cell: {}", a_store.last_state->cell).c_str());
            ImGui::Separator();
        }

        if (a_store.connection) {
            using namespace std::chrono;
            auto dur = duration_cast<seconds>(
                steady_clock::now() - a_store.connection->first_seen).count();
            ImGui::TextUnformatted(
                fmt::format("session {:02}:{:02}:{:02}",
                            dur / 3600, (dur / 60) % 60, dur % 60).c_str());
            ImGui::Separator();
        }

        // Right-justify diagnostics + save button.
        const auto recs = fmt::format("{} rec  {:.1f} MB",
                                      a_store.total_records,
                                      a_source.BytesIn() / 1024.0 / 1024.0);
        const auto txtSz = ImGui::CalcTextSize(recs.c_str());
        const float saveBtnW = 110.0f;
        const float marginPx = 16.0f;

        ImGui::SetCursorPosX(ImGui::GetWindowContentRegionMax().x
                             - txtSz.x - marginPx - saveBtnW);
        ImGui::TextDisabled("%s", recs.c_str());

        ImGui::SameLine();
        ImGui::SetCursorPosX(ImGui::GetWindowContentRegionMax().x - saveBtnW);
        const bool canSave = a_cb.on_save_session
            && a_source.CurrentState() == NdjsonSource::State::Connected;
        ImGui::BeginDisabled(!canSave);
        if (ImGui::Button("Save Session")) {
            a_state.save_popup_open = true;
            std::snprintf(a_state.save_name, sizeof(a_state.save_name),
                          "session");
        }
        ImGui::EndDisabled();

        // Transient "saved" confirmation: show for a few seconds after the
        // plugin acks a save, since the file lands in the SKSE log dir (not
        // next to the exe) and users otherwise can't tell it worked.
        if (a_store.last_saved_path) {
            const auto age = std::chrono::steady_clock::now() - a_store.last_saved_at;
            if (age < std::chrono::seconds{ 8 }) {
                ImGui::SameLine();
                ImGui::TextColored(ImVec4(0.4f, 0.9f, 0.4f, 1.0f), "saved");
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("%s", a_store.last_saved_path->c_str());
                }
            }
        }

        ImGui::EndMainMenuBar();
    }

    if (a_state.save_popup_open) {
        ImGui::OpenPopup("Save Session");
        a_state.save_popup_open = false;
    }
    if (ImGui::BeginPopupModal("Save Session", nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Pin the current rolling buffer to a permanent file.");
        ImGui::TextDisabled(
            "Saved into the SKSE log folder, not next to this viewer:");
        ImGui::TextDisabled(
            "Documents\\My Games\\<Skyrim>\\SKSE\\skygraph\\");
        ImGui::Separator();
        ImGui::InputText("Name", a_state.save_name, sizeof(a_state.save_name));
        ImGui::Separator();
        if (ImGui::Button("Save", ImVec2(120, 0))) {
            if (a_cb.on_save_session && a_state.save_name[0] != 0) {
                a_cb.on_save_session(a_state.save_name);
            }
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120, 0))) {
            ImGui::CloseCurrentPopup();
        }

        // If we've saved at least once this session, show the resolved path
        // from the plugin's ack with quick copy / open-folder actions.
        if (a_store.last_saved_path) {
            ImGui::Separator();
            ImGui::TextDisabled("Last saved:");
            ImGui::TextWrapped("%s", a_store.last_saved_path->c_str());
            if (ImGui::Button("Copy path")) {
                ImGui::SetClipboardText(a_store.last_saved_path->c_str());
            }
            if (a_cb.on_reveal_path) {
                ImGui::SameLine();
                if (ImGui::Button("Open folder")) {
                    a_cb.on_reveal_path(*a_store.last_saved_path);
                }
            }
        }
        ImGui::EndPopup();
    }
}

}  // namespace skygraph::viewer::panels::status_bar
