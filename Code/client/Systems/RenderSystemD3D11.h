#pragma once

struct ImguiService;
struct IDXGISwapChain;
struct ID3D11Device;
struct OverlayService;

/**
 * @brief Runs the renderer for D3D11.
 */
struct RenderSystemD3D11
{
    RenderSystemD3D11(OverlayService& aOverlay, ImguiService& aImguiService);
    ~RenderSystemD3D11();

    TP_NOCOPYMOVE(RenderSystemD3D11);

    [[nodiscard]] HWND GetWindow() const;
    [[nodiscard]] IDXGISwapChain* GetSwapChain() const;

    // to make yamashi mad
    void OnDeviceCreation(IDXGISwapChain* apSwapChain, ID3D11Device* apDevice);
    void OnRender();
    void OnReset(IDXGISwapChain* apSwapChain);

    ID3D11Device* m_pDevice; // todo: getter

private:
    IDXGISwapChain* m_pSwapChain;
    OverlayService& m_overlay;
    ImguiService& m_imguiService;
    size_t m_createConnection;
    size_t m_renderConnection;
    size_t m_resetConnection;
};
