
#include <BSGraphics/BSGraphicsRenderer.h>

#include <Games/GamePatch.h>

struct BSInputDeviceManager;

void (*BSInputDeviceManager_PollInputDevices)(BSInputDeviceManager*, float) = nullptr;

void Hook_BSInputDeviceManager_PollInputDevices(BSInputDeviceManager* inputDeviceMgr, float afDelta)
{
    // GetMainWindow() stays null until the renderer hook has run. Skipping the
    // focus check for those first frames is harmless; reading through the null
    // pointer would take the game down with us.
    auto* pWindow = BSGraphics::GetMainWindow();
    if (pWindow && !pWindow->IsForeground())
        return;

    BSInputDeviceManager_PollInputDevices(inputDeviceMgr, afDelta);
}

static TiltedPhoques::Initializer s_initInputDeviceManager(
    []()
    {
        auto* pPollInputDevices = GamePatch::Anchor(68617, "input device poll");
        if (!pPollInputDevices)
            return;

        BSInputDeviceManager_PollInputDevices =
            reinterpret_cast<decltype(BSInputDeviceManager_PollInputDevices)>(pPollInputDevices);

        TP_HOOK_IMMEDIATE(&BSInputDeviceManager_PollInputDevices, &Hook_BSInputDeviceManager_PollInputDevices);
    });
