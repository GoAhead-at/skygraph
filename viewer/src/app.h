#pragma once

#include "state/telemetry_store.h"
#include "transport/ndjson_source.h"

#include <memory>
#include <string>
#include <vector>

struct ID3D11Device;
struct ID3D11DeviceContext;
struct IDXGISwapChain;
struct ID3D11RenderTargetView;
struct HWND__;
using HWND = HWND__*;

namespace skygraph::viewer {

// Owns the Win32 window, DX11 swap chain, ImGui+ImPlot context, and the live
// NDJSON source. Runs a single-threaded UI loop until the window is closed.
class App {
public:
    struct Options {
        std::wstring title{ L"skygraph" };
        int width{ 1600 };
        int height{ 900 };
        // If non-empty, opens this file as a replay source instead of the
        // live pipe. (Replay backend lands in the `replay` phase; this field
        // is reserved now so we don't have to thread it through later.)
        std::string replay_path;
        // Pipe name override (defaults to protocol::kPipeName).
        std::string pipe_name;
    };

    explicit App(Options a_opts);
    ~App();

    int Run();

    // Called from WndProc when the window is resized. Public so the static
    // WndProc can forward the event; not part of the normal API surface.
    void OnResize(unsigned a_w, unsigned a_h);

private:
    bool InitWindow();
    bool InitD3D();
    void InitImGui();
    void ShutdownD3D();
    void ShutdownWindow();
    void RebuildRenderTarget();

    void Frame();
    void DrawUi();

    Options _opts;
    HWND _hwnd{ nullptr };
    ID3D11Device* _device{ nullptr };
    ID3D11DeviceContext* _ctx{ nullptr };
    IDXGISwapChain* _swap{ nullptr };
    ID3D11RenderTargetView* _rtv{ nullptr };

    bool _running{ true };
    bool _swapChainOccluded{ false };

    TelemetryStore _store;
    std::unique_ptr<NdjsonSource> _source;
    std::vector<nlohmann::json> _drainScratch;

    // Source-swapping helpers (called from File menu / args parser).
    void OpenLivePipe();
    void OpenReplayFile(std::string a_path);
    // Returns the picked path or empty string.
    std::string PromptOpenReplayDialog();
};

}  // namespace skygraph::viewer
