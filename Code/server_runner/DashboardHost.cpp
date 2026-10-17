#include "DashboardHost.h"

// This translation unit is Windows-only: it owns a D3D11 device and the Win32
// ImGui backend. The build already excludes it from non-Windows targets, and
// this guard is the second line of defence so a stray glob can never drag
// <d3d11.h> into a Linux compile.
#ifdef _WIN32

// Windows first, in the order the Win32 backend expects.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <d3d11.h>
#include <tchar.h>

#include <imgui.h>
#include <imgui_impl_dx11.h>
#include <imgui_impl_win32.h>

#include "../server_ui/src/ui/DashboardImGui.h"
#include "../server_ui/src/ui/DashboardModel.h"

#include <chrono>
#include <cstdio>

// Forward declared by imgui_impl_win32.cpp; the header does not export it.
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace server_runner
{
namespace
{

struct HostState
{
    HWND hwnd{nullptr};
    ID3D11Device* device{nullptr};
    ID3D11DeviceContext* context{nullptr};
    IDXGISwapChain* swapChain{nullptr};
    ID3D11RenderTargetView* renderTarget{nullptr};
    bool running{true};
    serverui::DashboardModel* model{nullptr};
    DashboardHostActions actions;
};

HostState g_host;

void CreateRenderTarget()
{
    ID3D11Texture2D* pBackBuffer = nullptr;
    if (SUCCEEDED(g_host.swapChain->GetBuffer(0, IID_PPV_ARGS(&pBackBuffer))) && pBackBuffer != nullptr)
    {
        g_host.device->CreateRenderTargetView(pBackBuffer, nullptr, &g_host.renderTarget);
        pBackBuffer->Release();
    }
}

void CleanupRenderTarget()
{
    if (g_host.renderTarget != nullptr)
    {
        g_host.renderTarget->Release();
        g_host.renderTarget = nullptr;
    }
}

bool CreateDeviceD3D(HWND aHwnd)
{
    DXGI_SWAP_CHAIN_DESC sd{};
    sd.BufferCount = 2;
    sd.BufferDesc.Width = 0;
    sd.BufferDesc.Height = 0;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate.Numerator = 60;
    sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = aHwnd;
    sd.SampleDesc.Count = 1;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0};
    D3D_FEATURE_LEVEL obtained = D3D_FEATURE_LEVEL_11_0;

    const HRESULT hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0,
        levels, 2, D3D11_SDK_VERSION, &sd, &g_host.swapChain, &g_host.device, &obtained, &g_host.context);

    if (FAILED(hr))
        return false;

    CreateRenderTarget();
    return g_host.renderTarget != nullptr;
}

void CleanupDeviceD3D()
{
    CleanupRenderTarget();
    if (g_host.swapChain != nullptr)
    {
        g_host.swapChain->Release();
        g_host.swapChain = nullptr;
    }
    if (g_host.context != nullptr)
    {
        g_host.context->Release();
        g_host.context = nullptr;
    }
    if (g_host.device != nullptr)
    {
        g_host.device->Release();
        g_host.device = nullptr;
    }
}

LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
        return true;

    switch (msg)
    {
    case WM_SIZE:
        if (g_host.device != nullptr && wParam != SIZE_MINIMIZED)
        {
            CleanupRenderTarget();
            g_host.swapChain->ResizeBuffers(0, static_cast<UINT>(LOWORD(lParam)),
                static_cast<UINT>(HIWORD(lParam)), DXGI_FORMAT_UNKNOWN, 0);
            CreateRenderTarget();
        }
        return 0;

    case WM_SYSCOMMAND:
        if ((wParam & 0xfff0) == SC_KEYMENU)
            return 0;
        break;

    case WM_CLOSE:
        // Closing the window is a graceful shutdown request, not a hard kill:
        // the caller stops the core after this returns.
        g_host.running = false;
        if (g_host.actions.gracefulShutdown != nullptr)
            g_host.actions.gracefulShutdown(g_host.actions.userData);
        return 0;

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

} // namespace

int RunDashboardWindow(serverui::DashboardModel& aModel, const DashboardHostActions& acActions,
    const wchar_t* apTitle, int aWidth, int aHeight)
{
    g_host.model = &aModel;
    g_host.actions = acActions;

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_CLASSDC;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = L"STRDashboardHost";
    RegisterClassExW(&wc);

    g_host.hwnd = CreateWindowW(wc.lpszClassName, apTitle, WS_OVERLAPPEDWINDOW,
        100, 100, aWidth, aHeight, nullptr, nullptr, wc.hInstance, nullptr);

    if (g_host.hwnd == nullptr)
        return 1;

    if (!CreateDeviceD3D(g_host.hwnd))
    {
        // No D3D11: report it so the caller can fall back to a console run.
        CleanupDeviceD3D();
        DestroyWindow(g_host.hwnd);
        UnregisterClassW(wc.lpszClassName, wc.hInstance);
        return 1;
    }

    ShowWindow(g_host.hwnd, SW_SHOWDEFAULT);
    UpdateWindow(g_host.hwnd);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    serverui::ApplyDashboardTheme();

    ImGui_ImplWin32_Init(g_host.hwnd);
    ImGui_ImplDX11_Init(g_host.device, g_host.context);

    // Wall clock for the model's duration cells. The model takes the timestamp
    // as a parameter precisely so it stays deterministic in tests; here it gets
    // the real one.
    const auto epoch = std::chrono::steady_clock::now();

    MSG msg;
    while (g_host.running)
    {
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE))
        {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
            if (msg.message == WM_QUIT)
                g_host.running = false;
        }

        if (!g_host.running)
            break;

        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        RECT rect{};
        GetClientRect(g_host.hwnd, &rect);
        const float width = static_cast<float>(rect.right - rect.left);
        const float height = static_cast<float>(rect.bottom - rect.top);

        serverui::DashboardActions actions;
        actions.gracefulShutdown = [&acActions]()
        {
            if (acActions.gracefulShutdown != nullptr)
                acActions.gracefulShutdown(acActions.userData);
        };
        actions.submitCommand = [&acActions](std::string_view aLine)
        {
            if (acActions.submitCommand != nullptr)
            {
                const std::string owned(aLine);
                acActions.submitCommand(acActions.userData, owned.c_str());
            }
        };

        // Refresh first, on this thread, then draw what was just refreshed.
        if (acActions.beforeFrame != nullptr)
            acActions.beforeFrame(aModel);

        serverui::DrawDashboard(aModel, actions, width, height);

        ImGui::Render();

        const float clear[4] = {0.055f, 0.063f, 0.078f, 1.0f};
        g_host.context->OMSetRenderTargets(1, &g_host.renderTarget, nullptr);
        g_host.context->ClearRenderTargetView(g_host.renderTarget, clear);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        g_host.swapChain->Present(1, 0);

        (void)epoch;
    }

    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();

    CleanupDeviceD3D();
    DestroyWindow(g_host.hwnd);
    UnregisterClassW(wc.lpszClassName, wc.hInstance);

    return 0;
}

} // namespace server_runner

#endif // _WIN32
