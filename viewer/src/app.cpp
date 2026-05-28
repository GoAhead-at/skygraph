#include "app.h"

#include "panels/charts_panel.h"
#include "panels/events_panel.h"
#include "panels/papyrus_panel.h"
#include "panels/plugins_panel.h"
#include "panels/status_bar.h"
#include "panels/stutter_panel.h"
#include "panels/timeline_panel.h"
#include "transport/file_source.h"
#include "transport/pipe_client.h"

#include <skygraph/protocol/messages.h>
#include <skygraph/protocol/pipe.h>

#include <imgui.h>
#include <imgui_impl_dx11.h>
#include <imgui_impl_win32.h>
#include <implot.h>

#include <spdlog/spdlog.h>

#include <Windows.h>
#include <commdlg.h>
#include <d3d11.h>
#include <dxgi1_2.h>

#include <filesystem>

// ImGui Win32 message handler forward decl (provided by imgui_impl_win32).
extern IMGUI_IMPL_API LRESULT
ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);

namespace skygraph::viewer {

namespace {

App* g_app = nullptr;

LRESULT CALLBACK WndProc(HWND a_hwnd, UINT a_msg, WPARAM a_w, LPARAM a_l) {
    if (ImGui_ImplWin32_WndProcHandler(a_hwnd, a_msg, a_w, a_l)) return true;
    switch (a_msg) {
        case WM_SIZE:
            if (g_app && a_w != SIZE_MINIMIZED) {
                g_app->OnResize(static_cast<unsigned>(LOWORD(a_l)),
                                static_cast<unsigned>(HIWORD(a_l)));
            }
            return 0;
        case WM_SYSCOMMAND:
            if ((a_w & 0xfff0) == SC_KEYMENU) return 0;  // disable alt menu
            break;
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProc(a_hwnd, a_msg, a_w, a_l);
}

}  // namespace

App::App(Options a_opts) : _opts{ std::move(a_opts) } {
    g_app = this;
    if (_opts.pipe_name.empty()) {
        _opts.pipe_name = skygraph::protocol::kPipeName;
    }
}

App::~App() {
    if (_source) _source->Stop();
    ShutdownD3D();
    ShutdownWindow();
    g_app = nullptr;
}

int App::Run() {
    if (!InitWindow()) return 1;
    if (!InitD3D()) return 2;
    InitImGui();

    if (!_opts.replay_path.empty()) {
        OpenReplayFile(_opts.replay_path);
    } else {
        OpenLivePipe();
    }

    ShowWindow(_hwnd, SW_SHOWDEFAULT);
    UpdateWindow(_hwnd);

    while (_running) {
        MSG msg;
        while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
            if (msg.message == WM_QUIT) _running = false;
        }
        if (!_running) break;

        if (_swapChainOccluded
            && _swap->Present(0, DXGI_PRESENT_TEST) == DXGI_STATUS_OCCLUDED) {
            Sleep(10);
            continue;
        }
        _swapChainOccluded = false;
        Frame();
    }
    return 0;
}

bool App::InitWindow() {
    WNDCLASSEXW wc{ sizeof(wc), CS_CLASSDC, WndProc, 0, 0,
                    GetModuleHandle(nullptr), nullptr,
                    LoadCursor(nullptr, IDC_ARROW), nullptr,
                    nullptr, L"skygraph_window", nullptr };
    RegisterClassExW(&wc);
    _hwnd = CreateWindowExW(0, wc.lpszClassName, _opts.title.c_str(),
                            WS_OVERLAPPEDWINDOW,
                            CW_USEDEFAULT, CW_USEDEFAULT,
                            _opts.width, _opts.height,
                            nullptr, nullptr, wc.hInstance, nullptr);
    return _hwnd != nullptr;
}

bool App::InitD3D() {
    DXGI_SWAP_CHAIN_DESC sd{};
    sd.BufferCount = 2;
    sd.BufferDesc.Width = 0;
    sd.BufferDesc.Height = 0;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate.Numerator = 60;
    sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = _hwnd;
    sd.SampleDesc.Count = 1;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;

    D3D_FEATURE_LEVEL featureLevel;
    const D3D_FEATURE_LEVEL levels[] = {
        D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0
    };
    HRESULT hr = D3D11CreateDeviceAndSwapChain(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0,
        levels, 2, D3D11_SDK_VERSION,
        &sd, &_swap, &_device, &featureLevel, &_ctx);
    if (hr == DXGI_ERROR_UNSUPPORTED) {
        hr = D3D11CreateDeviceAndSwapChain(
            nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0,
            levels, 2, D3D11_SDK_VERSION,
            &sd, &_swap, &_device, &featureLevel, &_ctx);
    }
    if (FAILED(hr)) {
        spdlog::error("D3D11CreateDeviceAndSwapChain failed: 0x{:08x}",
                      static_cast<std::uint32_t>(hr));
        return false;
    }
    RebuildRenderTarget();
    return true;
}

void App::OnResize(unsigned a_w, unsigned a_h) {
    if (!_swap || a_w == 0 || a_h == 0) return;
    if (_rtv) { _rtv->Release(); _rtv = nullptr; }
    _swap->ResizeBuffers(0, a_w, a_h, DXGI_FORMAT_UNKNOWN, 0);
    RebuildRenderTarget();
}

void App::RebuildRenderTarget() {
    if (_rtv) { _rtv->Release(); _rtv = nullptr; }
    ID3D11Texture2D* backBuffer = nullptr;
    _swap->GetBuffer(0, IID_PPV_ARGS(&backBuffer));
    if (backBuffer) {
        _device->CreateRenderTargetView(backBuffer, nullptr, &_rtv);
        backBuffer->Release();
    }
}

void App::InitImGui() {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImPlot::CreateContext();

    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

    // Use a local imgui.ini if present, else copy the default once.
    auto exeDir = []() -> std::filesystem::path {
        wchar_t buf[MAX_PATH];
        GetModuleFileNameW(nullptr, buf, MAX_PATH);
        return std::filesystem::path{ buf }.parent_path();
    }();
    auto userIni = exeDir / "imgui.ini";
    auto defaultIni = exeDir / "imgui.ini.default";
    if (!std::filesystem::exists(userIni)
        && std::filesystem::exists(defaultIni)) {
        std::error_code ec;
        std::filesystem::copy_file(defaultIni, userIni, ec);
    }

    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 4.0f;
    style.FrameRounding = 3.0f;
    style.GrabRounding = 2.0f;

    ImGui_ImplWin32_Init(_hwnd);
    ImGui_ImplDX11_Init(_device, _ctx);
}

void App::Frame() {
    // Drain the source into the store before rendering.
    _drainScratch.clear();
    _source->Drain(_drainScratch);
    for (auto& r : _drainScratch) {
        _store.Ingest(r);
    }

    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();

    // Full-viewport dockspace under the main menu bar.
    ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->WorkPos);
    ImGui::SetNextWindowSize(vp->WorkSize);
    ImGui::SetNextWindowViewport(vp->ID);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::Begin("skygraph::dockspace", nullptr,
                 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse
                     | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove
                     | ImGuiWindowFlags_NoBringToFrontOnFocus
                     | ImGuiWindowFlags_NoNavFocus
                     | ImGuiWindowFlags_NoDocking);
    ImGui::PopStyleVar(3);
    ImGui::DockSpace(ImGui::GetID("skygraph::root"), ImVec2(0, 0),
                     ImGuiDockNodeFlags_PassthruCentralNode);
    ImGui::End();

    DrawUi();

    // Render.
    const float clear[4] = { 0.07f, 0.07f, 0.08f, 1.0f };
    _ctx->OMSetRenderTargets(1, &_rtv, nullptr);
    _ctx->ClearRenderTargetView(_rtv, clear);
    ImGui::Render();
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

    HRESULT hr = _swap->Present(1, 0);
    _swapChainOccluded = (hr == DXGI_STATUS_OCCLUDED);
}

void App::DrawUi() {
    static panels::status_bar::State statusState;
    panels::status_bar::Callbacks cb;
    cb.on_save_session = [this](const std::string& a_name) {
        if (auto* pc = dynamic_cast<PipeClient*>(_source.get())) {
            nlohmann::json j = {
                { skygraph::protocol::cmd::kField,
                  skygraph::protocol::cmd::kSaveSession },
                { "name", a_name },
            };
            pc->SendCommand(j.dump());
        }
    };
    cb.on_open_replay = [this] {
        auto p = PromptOpenReplayDialog();
        if (!p.empty()) OpenReplayFile(std::move(p));
    };
    cb.on_connect_live = [this] { OpenLivePipe(); };
    cb.on_exit = [this] { _running = false; };
    panels::status_bar::Draw(_store, *_source, statusState, cb);

    // Foundation phase: minimal panels so the docking layout has something
    // to bind to. Real chart/papyrus/event/stutter panels land in later
    // phases.
    if (ImGui::Begin("Charts")) {
        panels::charts::Draw(_store);
    }
    ImGui::End();

    static panels::papyrus::State papyrusState;
    if (ImGui::Begin("Papyrus")) {
        panels::papyrus::Draw(_store, papyrusState);
    }
    ImGui::End();

    static panels::stutter::State stutterState;
    if (ImGui::Begin("Stutters")) {
        panels::stutter::Draw(_store, stutterState);
    }
    ImGui::End();

    static panels::events::State eventsState;
    if (ImGui::Begin("Events")) {
        panels::events::Draw(_store, eventsState);
    }
    ImGui::End();

    static panels::plugins::State pluginsState;
    if (ImGui::Begin("Plugins")) {
        panels::plugins::Draw(_store, pluginsState);
    }
    ImGui::End();

    static panels::timeline::State timelineState;
    if (ImGui::Begin("Timeline")) {
        panels::timeline::Draw(_store, *_source, timelineState);
    }
    ImGui::End();
}

void App::OpenLivePipe() {
    if (_source) _source->Stop();
    _store.Clear();
    auto client = std::make_unique<PipeClient>(_opts.pipe_name);
    client->Start();
    _source = std::move(client);
}

void App::OpenReplayFile(std::string a_path) {
    if (_source) _source->Stop();
    _store.Clear();
    auto fs = std::make_unique<FileSource>(std::move(a_path));
    fs->Start();
    _source = std::move(fs);
}

std::string App::PromptOpenReplayDialog() {
    OPENFILENAMEW ofn{};
    wchar_t buf[MAX_PATH] = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = _hwnd;
    ofn.lpstrFilter = L"Skygraph sessions\0*.ndjson;*.ndjson.gz\0All files\0*.*\0";
    ofn.lpstrFile = buf;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrTitle = L"Open recorded session";
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (!GetOpenFileNameW(&ofn)) return {};

    char utf8[MAX_PATH * 4] = {};
    WideCharToMultiByte(CP_UTF8, 0, buf, -1, utf8, sizeof(utf8), nullptr, nullptr);
    return utf8;
}

void App::ShutdownD3D() {
    if (ImGui::GetCurrentContext()) {
        ImGui_ImplDX11_Shutdown();
        ImGui_ImplWin32_Shutdown();
        ImPlot::DestroyContext();
        ImGui::DestroyContext();
    }
    if (_rtv) { _rtv->Release(); _rtv = nullptr; }
    if (_swap) { _swap->Release(); _swap = nullptr; }
    if (_ctx) { _ctx->Release(); _ctx = nullptr; }
    if (_device) { _device->Release(); _device = nullptr; }
}

void App::ShutdownWindow() {
    if (_hwnd) {
        DestroyWindow(_hwnd);
        _hwnd = nullptr;
    }
    UnregisterClassW(L"skygraph_window", GetModuleHandle(nullptr));
}

}  // namespace skygraph::viewer
