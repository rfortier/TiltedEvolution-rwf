#pragma once

struct OverlayService;

/**
 * @brief Handles input handling for the UI.
 */
struct InputService
{
    InputService(OverlayService& aOverlay) noexcept;
    ~InputService() noexcept;

    static LRESULT WndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

    /**
     * @brief Turns the overlay's input capture on or off.
     *
     * Use this instead of DInputHook::SetEnabled directly. Disabling the
     * hook also tears down the raw input registration, and the F2 toggle is
     * delivered as WM_INPUT, so the registration has to be repaired
     * afterwards or the next press has nothing to arrive on.
     */
    static void SetCaptureEnabled(bool aEnabled) noexcept;

    TP_NOCOPYMOVE(InputService);
};
