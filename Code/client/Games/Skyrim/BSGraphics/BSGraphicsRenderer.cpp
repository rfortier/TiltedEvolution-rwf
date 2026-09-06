
#include "Services/InputService.h"
#include "Systems/RenderSystemD3D11.h"

#include "World.h"

#include "BSGraphics/BSGraphicsRenderer.h"
#include "BSRandom/BSRandom.h"
#include "Games/GamePatch.h"

// shared resource by launcher
extern HICON g_SharedWindowIcon;

namespace BSGraphics
{
namespace
{

static RenderSystemD3D11* g_sRs = nullptr;
static WNDPROC RealWndProc = nullptr;
static RendererWindow* g_RenderWindow = nullptr;

// The window DXGI says the swapchain presents to. RendererWindow::hWnd holds
// the same handle, but this one cannot disagree with the surface the overlay
// is actually drawn on, so prefer it once the renderer has come up.
static HWND g_MainWindowHandle = nullptr;

static constexpr char kTogetherWindowName[]{"Skyrim Together"};

// Reads the swapchain description, treating a bad pointer as "no swapchain"
// rather than a crash. The struct offsets themselves are sound on 1.5.x - the
// pre-AE BGSRenderer model this code base used before the AE migration put the
// device at +0x38, the window at +0x48 and the swapchain at +0x60 from the
// same base, which is exactly what RendererData computes today (see the
// static_asserts in the header) - but the renderer can still fail and leave
// the field null, and that has to be visible in the log instead of showing up
// as a menu that never opens.
bool DescribeSwapChain(IDXGISwapChain* apSwapChain, DXGI_SWAP_CHAIN_DESC& aDesc) noexcept
{
    if (!apSwapChain)
        return false;

    HRESULT hr = E_FAIL;
    __try
    {
        hr = apSwapChain->GetDesc(&aDesc);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }

    return SUCCEEDED(hr) && aDesc.OutputWindow != nullptr;
}

} // namespace
RendererWindow* GetMainWindow()
{
    return g_RenderWindow;
}

bool RendererWindow::IsForeground()
{
    const HWND window = g_MainWindowHandle ? g_MainWindowHandle : hWnd;
    return GetForegroundWindow() == window;
}

void (*Renderer_Init)(Renderer*, BSGraphics::RendererInitOSData*, const BSGraphics::ApplicationWindowProperties*, BSGraphics::RendererInitReturn*) = nullptr;

// WNDPROC seems to be part of the renderer
LRESULT CALLBACK Hook_WndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    if (InputService::WndProc(hwnd, uMsg, wParam, lParam) != 0)
        return 0;

    return RealWndProc(hwnd, uMsg, wParam, lParam);
}

void Hook_Renderer_Init(Renderer* self, BSGraphics::RendererInitOSData* aOSData, const BSGraphics::ApplicationWindowProperties* aFBData, BSGraphics::RendererInitReturn* aOut)
{
    // we feed this a shared icon as the resource directory of our former launcher data is already overwritten with the
    // game.
    aOSData->hIcon = g_SharedWindowIcon;
    // Append our window name.
    aOSData->pClassName = kTogetherWindowName;

    RealWndProc = aOSData->pWndProc;
    aOSData->pWndProc = Hook_WndProc;

    Renderer_Init(self, aOSData, aFBData, aOut);

    g_sRs = &World::Get().ctx().at<RenderSystemD3D11>();
    // This how the game does it too
    g_RenderWindow = &self->Data.RenderWindowA[0];

    const BSGraphics::RendererData& renderer = self->Data;

    IDXGISwapChain* pSwapChain = renderer.RenderWindowA[0].pSwapChain;
    ID3D11Device* pDevice = renderer.pForwarder;
    ID3D11DeviceContext* pContext = renderer.pContext;

    DXGI_SWAP_CHAIN_DESC desc{};
    if (!DescribeSwapChain(pSwapChain, desc))
    {
        spdlog::error("renderer init: no usable swapchain at the expected offset ({}), the overlay cannot start",
                      fmt::ptr(pSwapChain));
        return;
    }

    // The window and the device both belong to the swapchain, so take them
    // from there instead of trusting two more struct offsets.
    g_MainWindowHandle = desc.OutputWindow;

    ID3D11Device* pOwner = nullptr;
    if (SUCCEEDED(pSwapChain->GetDevice(__uuidof(ID3D11Device), reinterpret_cast<void**>(&pOwner))) && pOwner)
    {
        pDevice = pOwner;
        pContext = nullptr;
        pOwner->GetImmediateContext(&pContext);
    }

    spdlog::info("renderer init: swapchain {}, device {}, context {}, window {}", fmt::ptr(pSwapChain),
                 fmt::ptr(pDevice), fmt::ptr(pContext), fmt::ptr(g_MainWindowHandle));

    if (!pDevice || !pContext)
    {
        spdlog::error("renderer init: no D3D11 device/context, the overlay cannot start");
        return;
    }

    g_sRs->OnDeviceCreation(pSwapChain, pDevice, pContext);
}

// The overlay is pumped from the game's frame end. Report the first tick: if
// this line is missing from the log the UI can never show up, no matter what
// the rest of the client is doing.
void PumpOverlay()
{
    static bool s_firstFrame = true;
    if (s_firstFrame)
    {
        s_firstFrame = false;
        spdlog::info("overlay render pump is live");
    }

    if (g_sRs)
        g_sRs->OnRender();
}

void (*StopTimer)(int) = nullptr;

// Insert us at the End
void Hook_StopTimer(int type)
{
    PumpOverlay();

    StopTimer(type);
}

// 1.5.97 does not reach the pump through that call site. Both builds do open
// id 77246 with sub rsp / mov ecx,1 / call rel32, so swapping the call at +9
// writes cleanly there and reports success - and then never fires, because on
// 1.5.97 it is not the callee the frame end runs through. Detour the whole
// function instead, which is what this code base did while it still targeted
// 1.5.97: Games/Renderer.cpp before commit 8eaca858 hooked RVA 0xD6A2B0 as
// s_realRenderPresent, and that is exactly what id 77246 maps to.
void (*RenderFrameEnd)() = nullptr;

void Hook_RenderFrameEnd()
{
    PumpOverlay();

    RenderFrameEnd();
}

static TiltedPhoques::Initializer s_viewportHooks(
    []()
    {
        auto* pRendererInit = GamePatch::Anchor(77226, "renderer init");

        if (pRendererInit)
        {
            // patch dwStyle in BSGraphics::InitWindows so windowed mode keeps
            // its close button. 1.5.97 stores it from a different function
            // (RVA 0xD71FA9+1, outside this anchor), and the patch is only
            // cosmetic, so it is dropped there instead of writing into the
            // renderer init we are about to detour.
            GamePatch::Put<uint32_t>(GamePatch::At(pRendererInit, {0x174 + 1}, "window style"),
                                     WS_OVERLAPPEDWINDOW, "window style");
        }

        // TODO: move me to input patches.
        // don't let the game steal the media keys in windowed mode
        if (auto* pAcquireKeyboard = GamePatch::Anchor(68781, "dinput cooperative level"))
        {
            GamePatch::Put<uint32_t>(
                GamePatch::At(pAcquireKeyboard, {0x55 + 2}, "dinput cooperative level"),
                /*strip DISCL_EXCLUSIVE bits and append DISCL_NONEXCLUSIVE*/ 3u,
                "dinput cooperative level");
        }

        // The overlay is drawn from here; without it the UI never renders and
        // OverlayService never learns that the player is in game, which is
        // what gates the F2 toggle.
        if (auto* pFrameEnd = GamePatch::Anchor(77246, "frame end"))
        {
            if (VersionDb::Get().IsLegacyFormat())
            {
                RenderFrameEnd = reinterpret_cast<decltype(RenderFrameEnd)>(pFrameEnd);
                TP_HOOK_IMMEDIATE(&RenderFrameEnd, &Hook_RenderFrameEnd);
            }
            else
            {
                GamePatch::SwapCall(GamePatch::At(pFrameEnd, {9}, "frame end"),
                                    StopTimer, &Hook_StopTimer, "frame end");
            }
        }

        if (pRendererInit)
        {
            Renderer_Init = reinterpret_cast<decltype(Renderer_Init)>(pRendererInit);

            // Once we find a proper way to locate it for different versions, go back to swapcall
            // GamePatch::SwapCall(pRendererInit + 0xD1A, Renderer_Init, &Hook_Renderer_Init, "renderer init");
            TP_HOOK_IMMEDIATE(&Renderer_Init, &Hook_Renderer_Init);
        }
    });
} // namespace BSGraphics
