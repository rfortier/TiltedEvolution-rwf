#include <TiltedOnlinePCH.h>

#include <Services/InputService.h>
#include <Services/OverlayService.h>

#include <OverlayApp.hpp>

#include <DInputHook.hpp>
#include <WindowsHook.hpp>

#include <include/internal/cef_types.h>
#include <Services/ImguiService.h>
#include <Services/DiscordService.h>
#include <World.h>

#include "Games/Skyrim/Interface/MenuControls.h"

#include <imm.h>
#include <atomic>
#include <thread>

#pragma comment(lib, "imm32.lib")

static OverlayService* s_pOverlay = nullptr;
static UINT s_currentACP = CP_ACP;

// True while the active input method is in the middle of composing a character.
// While that is the case the raw-key -> character path below is disabled, so the
// latin keystrokes that spell a pinyin syllable do not leak into the text field
// as individual letters next to the composed result.
static bool s_imeComposing = false;

// The frame loop cannot be left to the game's own hooks: on 1.5.97 neither
// SkyrimVM::Update nor MainLoop is ever reached through them (both heartbeat
// counters stay at zero across a whole session), so World::Update() would never
// run and nothing would synchronise once connected. A timer on the game's own
// window is the one cadence we can force without knowing any game address, and
// the message pump that drives it is guaranteed alive because the window
// responds at all. On runtimes where the hooks do fire this is simply an extra
// update per interval; World::Update() is idempotent and measures its own delta.
static constexpr UINT_PTR kWorldUpdateTimerId = 0x5A17;
// SetTimer quantises to the 15.625 ms system tick, so the interval is rounded
// *up* to a multiple of it. 16 ms therefore becomes two ticks (31.25 ms, about
// 32 updates a second) while 8 ms fits inside one (15.625 ms, about 64). Remote
// movement is sampled on this cadence, so the halved interval is what removes
// the stepping visible on other players during interpolation.
static constexpr UINT kWorldUpdateTimerIntervalMs = 8;

static std::atomic<uint64_t> s_timerTicks{0};
static std::chrono::steady_clock::time_point s_lastTimerUpdate{};

void ForceKillAllInput()
{
    MenuControls::GetInstance()->SetToggle(false);
}

// The lock keys describe the layout rather than the stroke, so they are the
// only flags that survive into a character event. Windows folds every other
// modifier into the produced character before sending WM_CHAR, and Chromium
// applies them a second time if the event still carries them.
uint32_t GetCefLockModifiers()
{
    uint32_t modifiers = EVENTFLAG_NONE;

    if (GetKeyState(VK_CAPITAL) & 1)
    {
        modifiers |= EVENTFLAG_CAPS_LOCK_ON;
    }

    if (GetKeyState(VK_NUMLOCK) & 1)
    {
        modifiers |= EVENTFLAG_NUM_LOCK_ON;
    }

    return modifiers;
}

uint32_t GetCefModifiers(uint16_t aVirtualKey)
{
    uint32_t modifiers = EVENTFLAG_NONE;

    if (GetKeyState(VK_MENU) & 0x8000)
    {
        modifiers |= EVENTFLAG_ALT_DOWN;
    }

    if (GetKeyState(VK_CONTROL) & 0x8000)
    {
        modifiers |= EVENTFLAG_CONTROL_DOWN;
    }

    if (GetKeyState(VK_SHIFT) & 0x8000)
    {
        modifiers |= EVENTFLAG_SHIFT_DOWN;
    }

    if (GetKeyState(VK_LBUTTON) & 0x8000)
    {
        modifiers |= EVENTFLAG_LEFT_MOUSE_BUTTON;
    }

    if (GetKeyState(VK_RBUTTON) & 0x8000)
    {
        modifiers |= EVENTFLAG_RIGHT_MOUSE_BUTTON;
    }

    if (GetKeyState(VK_MBUTTON) & 0x8000)
    {
        modifiers |= EVENTFLAG_MIDDLE_MOUSE_BUTTON;
    }

    if (GetKeyState(VK_CAPITAL) & 1)
    {
        modifiers |= EVENTFLAG_CAPS_LOCK_ON;
    }

    if (GetKeyState(VK_NUMLOCK) & 1)
    {
        modifiers |= EVENTFLAG_NUM_LOCK_ON;
    }

    if (aVirtualKey)
    {
        if (aVirtualKey == VK_RCONTROL || aVirtualKey == VK_RMENU || aVirtualKey == VK_RSHIFT)
        {
            modifiers |= EVENTFLAG_IS_RIGHT;
        }
        else if (aVirtualKey == VK_LCONTROL || aVirtualKey == VK_LMENU || aVirtualKey == VK_LSHIFT)
        {
            modifiers |= EVENTFLAG_IS_LEFT;
        }
        else if (aVirtualKey >= VK_NUMPAD0 && aVirtualKey <= VK_DIVIDE)
        {
            modifiers |= EVENTFLAG_IS_KEY_PAD;
        }
    }

    return modifiers;
}

// F2 is the only key that opens the multiplayer menu. Right Ctrl used to be an
// alias for it; it is gone because a second binding is a second thing to
// support, and because right Ctrl is a key players already use for other mods.
bool IsToggleKey(int aKey) noexcept
{
    return aKey == VK_F2;
}

bool IsDisableKey(int aKey) noexcept
{
    return aKey == VK_ESCAPE;
}

// The game reads the keyboard through DirectInput and never registers raw
// input itself, so this registration is ours alone to own. It has to be on
// the thread that owns the game window: that is the only thread WM_INPUT is
// delivered to.
static void RegisterRawInput() noexcept
{
    RAWINPUTDEVICE device[2];

    device[0].usUsagePage = 0x01;
    device[0].usUsage = 0x06;
    device[0].dwFlags = 0;
    device[0].hwndTarget = nullptr;

    device[1].usUsagePage = 0x01;
    device[1].usUsage = 0x02;
    device[1].dwFlags = 0;
    device[1].hwndTarget = nullptr;

    RegisterRawInputDevices(device, 2, sizeof(RAWINPUTDEVICE));
}

void InputService::SetCaptureEnabled(bool aEnabled) noexcept
{
    TiltedPhoques::DInputHook::Get().SetEnabled(aEnabled);

    // DInputHook::Update() issues an explicit RIDEV_REMOVE every time the
    // hook is switched off, and only re-registers while it is on. Left as
    // is, the first F2 after each close was spent re-arming raw input
    // through the DirectInput path instead of reaching this code, which is
    // what made the toggle fire only sometimes.
    RegisterRawInput();
}

void SetUIActive(OverlayService& aOverlay, auto apRenderer, bool aActive)
{
    InputService::SetCaptureEnabled(aActive);
    aOverlay.SetActive(aActive);

    // Ensures the game is actually loaded, in case the initial event was sent too early
    aOverlay.SetVersion(BUILD_COMMIT);
    if (auto* pApp = aOverlay.GetOverlayApp())
        pApp->ExecuteAsync("enterGame");

    apRenderer->SetCursorVisible(aActive);

    // This is to disable the Windows cursor
    while (ShowCursor(FALSE) >= 0)
        ;
}

void ProcessKeyboard(uint16_t aKey, uint16_t aScanCode, cef_key_event_type_t aType, bool aE0, bool aE1, uint16_t aCharacter = 0)
{
    if (aType != KEYEVENT_CHAR)
    {
        if (!aKey || aKey == 255)
        {
            return;
        }

        if (aKey == VK_SHIFT)
        {
            aKey = static_cast<uint16_t>(MapVirtualKey(aScanCode, MAPVK_VSC_TO_VK_EX));
        }
        else if (aKey == VK_NUMLOCK)
        {
            aScanCode = static_cast<uint16_t>(MapVirtualKey(aKey, MAPVK_VK_TO_VSC) | 0x100);
        }

        if (aE1)
        {
            if (aKey == VK_PAUSE)
            {
                aScanCode = 0x45;
            }
            else
            {
                aScanCode = static_cast<uint16_t>(MapVirtualKey(aKey, MAPVK_VK_TO_VSC));
            }
        }

        if (aE0)
        {
            switch (aKey)
            {
            case VK_CONTROL: aKey = VK_RCONTROL; break;
            case VK_MENU: aKey = VK_RMENU; break;
            case VK_RETURN: aKey = VK_SEPARATOR; break;
            }
        }
        else
        {
            switch (aKey)
            {
            case VK_CONTROL: aKey = VK_LCONTROL; break;
            case VK_MENU: aKey = VK_LMENU; break;
            case VK_INSERT: aKey = VK_NUMPAD0; break;
            case VK_DELETE: aKey = VK_DECIMAL; break;
            case VK_HOME: aKey = VK_NUMPAD7; break;
            case VK_END: aKey = VK_NUMPAD1; break;
            case VK_PRIOR: aKey = VK_NUMPAD9; break;
            case VK_NEXT: aKey = VK_NUMPAD3; break;
            case VK_LEFT: aKey = VK_NUMPAD4; break;
            case VK_RIGHT: aKey = VK_NUMPAD6; break;
            case VK_UP: aKey = VK_NUMPAD8; break;
            case VK_DOWN: aKey = VK_NUMPAD2; break;
            case VK_CLEAR: aKey = VK_NUMPAD5; break;
            }
        }
    }

    auto& overlay = *s_pOverlay;

    const auto pApp = overlay.GetOverlayApp();
    if (!pApp)
        return;

    const auto pClient = pApp->GetClient();
    if (!pClient)
        return;

    const auto pRenderer = pClient->GetOverlayRenderHandler();
    if (!pRenderer)
        return;

    const auto active = overlay.GetActive();

    spdlog::debug("ProcessKey, type: {}, key: {}, active: {}", aType, aKey, active);

    if (aType != KEYEVENT_CHAR && (IsToggleKey(aKey) || (IsDisableKey(aKey) && active)))
    {
        if (!overlay.GetInGame())
        {
            InputService::SetCaptureEnabled(false);
        }
        else if (aType == KEYEVENT_KEYUP)
        {
            SetUIActive(overlay, pRenderer, !active);
        }
    }
    else if (active && aType != KEYEVENT_CHAR)
    {
        pApp->InjectKey(aType, GetCefModifiers(aKey), aKey, aScanCode);
    }

    // InjectKey has no room for the text a keystroke produces, and the
    // submodule cannot be patched here (CI force-updates it from upstream),
    // so the character event is sent straight to the browser instead. Doing
    // it after InjectKey keeps the ordering the page expects: keydown first,
    // then the character it produced.
    //
    // The InjectKey branch above excludes KEYEVENT_CHAR: CefKeyEvent's
    // `character` would be left uninitialised there, and the browser would
    // receive a second, garbage character between the keydown and the real
    // input.
    if (aType == KEYEVENT_CHAR && active && aCharacter)
    {
        if (const auto pBrowser = pClient->GetBrowser(); pBrowser && pBrowser->GetHost())
        {
            CefKeyEvent ev;
            ev.type = KEYEVENT_CHAR;
            // A character event carries no key state. By the time Windows
            // would have sent WM_CHAR the modifier has already been folded
            // into the character itself, so SHIFT must not be set here:
            // Chromium peels an attached Shift back off again, which turned
            // Shift+1 into "1" and Shift+a into "a". GetCefModifiers() is
            // deliberately not used, it reports the physical modifier state
            // of the key that produced this text.
            ev.modifiers = GetCefLockModifiers();
            // For a character event CEF reads the text out of
            // `windows_key_code`, not out of `character`. Leaving the
            // virtual key there (aKey) makes every punctuation mark come out
            // as the character with the same numeric value as its VK code:
            // VK_OEM_PERIOD is 0xBE, so "." typed U+00BE "3/4". Digits only
            // appeared to work because VK '1'..'9' (0x31..0x39) coincide with
            // their ASCII code points. The produced code unit has to go in
            // both fields; the physical key is still described by
            // `native_key_code`.
            ev.windows_key_code = aCharacter;
            ev.native_key_code = aScanCode;
            ev.character = aCharacter;
            ev.unmodified_character = aCharacter;

            pBrowser->GetHost()->SendKeyEvent(ev);
        }
    }
}

// The composed text an input method committed. Windows delivers it through
// WM_IME_COMPOSITION rather than through the WM_INPUT keyboard path, and it is
// already the final characters - no layout translation applies here. Each
// UTF-16 code unit goes out as its own character event, exactly the way a plain
// key would, so the renderer's focused text field receives what the IME made.
static void ProcessImeResult(const std::wstring& acText) noexcept
{
    for (const wchar_t cCharacter : acText)
    {
        if (cCharacter == L'\r' || cCharacter == L'\n')
            continue;

        ProcessKeyboard(0, 0, KEYEVENT_CHAR, false, false, static_cast<uint16_t>(cCharacter));
    }
}

// The overlay never sees WM_CHAR. The game owns the keyboard through
// DirectInput, its message loop does no TranslateMessage dispatch for us, and
// our window procedure is a subclass that only observes what the game's loop
// happens to pump. WM_INPUT is the one path we actually own, and it carries
// key state only, never text.
//
// So the character has to be produced here: translate the raw virtual key and
// scan code back into the text the layout would have generated.
static uint16_t TranslateToCharacter(uint16_t aScanCode, uint16_t aVirtualKey) noexcept
{
    // ToUnicodeEx needs the modifier state to decide between "1" and "!",
    // "a" and "A". GetKeyboardState is unreliable while the game owns the
    // device, so build the state from GetKeyState together with the toggle
    // keys, which is what the layout actually consults.
    BYTE keyboardState[256]{};
    auto& capslock = keyboardState[VK_CAPITAL];
    auto& numlock = keyboardState[VK_NUMLOCK];
    auto& scrolllock = keyboardState[VK_SCROLL];

    if (GetKeyState(VK_SHIFT) & 0x8000)
        keyboardState[VK_SHIFT] = 0x80;
    if (GetKeyState(VK_CONTROL) & 0x8000)
        keyboardState[VK_CONTROL] = 0x80;
    if (GetKeyState(VK_MENU) & 0x8000)
        keyboardState[VK_MENU] = 0x80;

    capslock = static_cast<BYTE>(GetKeyState(VK_CAPITAL) & 1);
    numlock = static_cast<BYTE>(GetKeyState(VK_NUMLOCK) & 1);
    scrolllock = static_cast<BYTE>(GetKeyState(VK_SCROLL) & 1);

    // A dead key (an accent waiting for its base letter) reports a negative
    // result and stays pending in the layout, so it is queried twice below.
    // The buffer is oversized because ToUnicodeEx may emit a whole AltGr
    // combination in one call.
    wchar_t buffer[8]{};
    constexpr int kCharacterBufferSize = static_cast<int>(sizeof(buffer) / sizeof(buffer[0]));

    int produced = ::ToUnicodeEx(aVirtualKey, aScanCode, keyboardState, buffer,
                                 kCharacterBufferSize, 0, GetKeyboardLayout(0));

    if (produced < 0)
    {
        // Flush the dead key, then report "nothing typed" for this stroke.
        // Leaving it pending would make the next ordinary key get swallowed.
        ::ToUnicodeEx(aVirtualKey, aScanCode, keyboardState, buffer,
                      kCharacterBufferSize, 0, GetKeyboardLayout(0));
        return 0;
    }

    if (produced <= 0)
        return 0;

    // ToUnicodeEx can emit several code units at once (a ligature, or an
    // AltGr combination). Only the first one is a plain keystroke into a
    // text field; the rest belong to the layout's own composition, which the
    // renderer handles on its own.
    return static_cast<uint16_t>(buffer[0]);
}

void ProcessMouseMove(uint16_t aX, uint16_t aY)
{
    auto& overlay = *s_pOverlay;

    const auto pApp = overlay.GetOverlayApp();
    if (!pApp)
        return;

    const auto pClient = pApp->GetClient();
    if (!pClient)
        return;

    const auto pRenderer = pClient->GetOverlayRenderHandler();
    if (!pRenderer)
        return;

    const auto active = overlay.GetActive();

    if (active)
    {
        pApp->InjectMouseMove(aX, aY, GetCefModifiers(0));
    }
}

void ProcessMouseButton(uint16_t aX, uint16_t aY, cef_mouse_button_type_t aButton, bool aDown)
{
    auto& overlay = *s_pOverlay;

    const auto pApp = overlay.GetOverlayApp();
    if (!pApp)
        return;

    const auto pClient = pApp->GetClient();
    if (!pClient)
        return;

    const auto pRenderer = pClient->GetOverlayRenderHandler();
    if (!pRenderer)
        return;

    const auto active = overlay.GetActive();

    if (active)
    {
        pApp->InjectMouseButton(aX, aY, aButton, !aDown, GetCefModifiers(0));
    }
}

void ProcessMouseWheel(uint16_t aX, uint16_t aY, int16_t aZ)
{
    auto& overlay = *s_pOverlay;

    const auto pApp = overlay.GetOverlayApp();
    if (!pApp)
        return;

    const auto pClient = pApp->GetClient();
    if (!pClient)
        return;

    const auto pRenderer = pClient->GetOverlayRenderHandler();
    if (!pRenderer)
        return;

    const auto active = overlay.GetActive();

    if (active)
    {
        pApp->InjectMouseWheel(aX, aY, aZ, GetCefModifiers(0));
    }
}

UINT GetRealACP()
{
    // Get the keyboard layout for the current thread.
    HKL keybdLayout = GetKeyboardLayout(0);

    // Extract the language ID from it, contained in its low-order word.
    int langID = LOWORD(keybdLayout);

    // Call the GetLocaleInfo function to retrieve the default ANSI code page
    // associated with that language ID.
    UINT acp = CP_ACP;
    GetLocaleInfo(MAKELCID(langID, SORT_DEFAULT),
        LOCALE_IDEFAULTANSICODEPAGE | LOCALE_RETURN_NUMBER,
        (LPTSTR) &acp,
        sizeof(acp) / sizeof(TCHAR));
    return acp;
}

LRESULT CALLBACK InputService::WndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    // The toggle key arrives as WM_INPUT. Nothing registers raw input before
    // the overlay is first opened, so without this the very first F2 has no
    // way to reach ProcessKeyboard - it only arms the hook through the
    // DirectInput path and opens nothing. Doing it here puts the
    // registration on the window's own thread instead of whichever thread
    // booted the client.
    static const bool s_rawInputRegistered = [] {
        RegisterRawInput();
        return true;
    }();
    (void)s_rawInputRegistered;

    // Arm the frame-loop fallback on the first message this window sees, which
    // is also the first moment hwnd is known for certain.
    static bool s_worldUpdateTimerArmed = false;
    if (!s_worldUpdateTimerArmed)
    {
        s_worldUpdateTimerArmed = true;
        ::SetTimer(hwnd, kWorldUpdateTimerId, kWorldUpdateTimerIntervalMs, nullptr);
    }

    if (uMsg == WM_TIMER && wParam == kWorldUpdateTimerId)
    {
        const auto cNow = std::chrono::steady_clock::now();
        if (s_lastTimerUpdate.time_since_epoch().count() == 0 ||
            cNow - s_lastTimerUpdate >= std::chrono::milliseconds(kWorldUpdateTimerIntervalMs))
        {
            s_lastTimerUpdate = cNow;

            const uint64_t cTick = s_timerTicks.fetch_add(1) + 1;
            if (cTick == 1 || cTick % 300 == 0)
            {
                spdlog::info("timer-driven update heartbeat: tick {}, thread id {}", cTick, ::GetCurrentThreadId());
            }

            // The game's own frame hooks only run the update while the VM is
            // active - never at the main menu or across a load screen. On 1.5.97
            // those hooks never fire at all, which is the whole reason this timer
            // exists, but arming it on the first window message means it ticks
            // long before a world or the player exist. Driving the update there
            // made the very first tick dereference the not-yet-created player
            // (read of [0x0 + 0x14], the player's formID) and crash on boot.
            // Gate on the signal the overlay already maintains: it only flips
            // true once the player has a 3D node, i.e. we are actually in game.
            if (s_pOverlay && s_pOverlay->GetInGame())
            {
                World::Get().Update();
            }
        }

        // This timer is ours; the game has no reason to see it.
        return 1;
    }

    const auto pApp = s_pOverlay->GetOverlayApp();
    if (!pApp)
        return 0;

    const auto pClient = pApp->GetClient();
    if (!pClient)
        return 0;

    const auto pRenderer = pClient->GetOverlayRenderHandler();
    if (!pRenderer)
        return 0;

    auto& discord = World::Get().ctx().at<DiscordService>();
    discord.WndProcHandler(hwnd, uMsg, wParam, lParam);

    const bool active = s_pOverlay->GetActive();
    if (active)
    {
        auto& imgui = World::Get().ctx().at<ImguiService>();
        imgui.WndProcHandler(hwnd, uMsg, wParam, lParam);
    }

    // Only pay for the cursor position when the overlay is actually up. This
    // WndProc also sees our own 16ms WM_TIMER, and InjectMouseMove crosses
    // into CEF's IPC queue, so doing it unconditionally put a synchronous
    // cross-process hop on the game thread once per frame for as long as the
    // game ran - with the overlay closed it had nowhere to go and was pure
    // cost. The position is still resolved below for the messages that need
    // it, all of which are already inside "active" checks.
    POINT position{};

    if (active)
    {
        GetCursorPos(&position);
        ScreenToClient(GetActiveWindow(), &position);

        ProcessMouseMove(static_cast<uint16_t>(position.x), static_cast<uint16_t>(position.y));
    }

    if (uMsg == WM_INPUT)
    {
        RAWINPUT input;
        UINT size = sizeof(RAWINPUT);

        GetRawInputData(reinterpret_cast<HRAWINPUT>(lParam), RID_INPUT, &input, &size, sizeof(RAWINPUTHEADER));

        if (active)
        {
            auto& imgui = World::Get().ctx().at<ImguiService>();
            imgui.RawInputHandler(input);
        }

        if (input.header.dwType == RIM_TYPEKEYBOARD)
        {
            const auto keyboard = input.data.keyboard;

            const bool isKeyUp = (keyboard.Flags & RI_KEY_BREAK) != 0;

            ProcessKeyboard(keyboard.VKey, keyboard.MakeCode, isKeyUp ? KEYEVENT_KEYUP : KEYEVENT_KEYDOWN, keyboard.Flags & RI_KEY_E0, keyboard.Flags & RI_KEY_E1);

            // Synthesise the character for the key-down edge of a printable
            // key. The game owns the keyboard and never runs TranslateMessage
            // for us, so WM_CHAR is not an option and this is the only place
            // the text can be produced. Skip anything with Ctrl or Alt held,
            // which is a shortcut rather than text, and skip the toggle keys
            // so opening the overlay cannot type into it.
            //
            // While an input method is composing, the same keystrokes belong
            // to it, not to the field: the composed result is delivered through
            // WM_IME_COMPOSITION instead, and translating the raw keys here on
            // top would type the pinyin letters next to the finished character.
            if (active && !isKeyUp && !s_imeComposing && !(GetKeyState(VK_CONTROL) & 0x8000) && !(GetKeyState(VK_MENU) & 0x8000))
            {
                if (!IsToggleKey(keyboard.VKey) && !IsDisableKey(keyboard.VKey))
                {
                    if (const uint16_t character = TranslateToCharacter(keyboard.MakeCode, keyboard.VKey))
                    {
                        ProcessKeyboard(keyboard.VKey, keyboard.MakeCode, KEYEVENT_CHAR, keyboard.Flags & RI_KEY_E0, keyboard.Flags & RI_KEY_E1, character);
                    }
                }
            }
        }
        else if (input.header.dwType == RIM_TYPEMOUSE)
        {
            const auto mouse = input.data.mouse;

            if (mouse.usButtonFlags & RI_MOUSE_WHEEL)
            {
                ProcessMouseWheel(static_cast<uint16_t>(position.x), static_cast<uint16_t>(position.y), (int16_t)mouse.usButtonData);
            }

            if (mouse.usButtonFlags & RI_MOUSE_LEFT_BUTTON_DOWN)
            {
                ProcessMouseButton(static_cast<uint16_t>(position.x), static_cast<uint16_t>(position.y), MBT_LEFT, true);
            }

            if (mouse.usButtonFlags & RI_MOUSE_LEFT_BUTTON_UP)
            {
                ProcessMouseButton(static_cast<uint16_t>(position.x), static_cast<uint16_t>(position.y), MBT_LEFT, false);
            }

            if (mouse.usButtonFlags & RI_MOUSE_RIGHT_BUTTON_DOWN)
            {
                ProcessMouseButton(static_cast<uint16_t>(position.x), static_cast<uint16_t>(position.y), MBT_RIGHT, true);
            }

            if (mouse.usButtonFlags & RI_MOUSE_RIGHT_BUTTON_UP)
            {
                ProcessMouseButton(static_cast<uint16_t>(position.x), static_cast<uint16_t>(position.y), MBT_RIGHT, false);
            }

            if (mouse.usButtonFlags & RI_MOUSE_MIDDLE_BUTTON_DOWN)
            {
                ProcessMouseButton(static_cast<uint16_t>(position.x), static_cast<uint16_t>(position.y), MBT_MIDDLE, true);
            }

            if (mouse.usButtonFlags & RI_MOUSE_MIDDLE_BUTTON_UP)
            {
                ProcessMouseButton(static_cast<uint16_t>(position.x), static_cast<uint16_t>(position.y), MBT_MIDDLE, false);
            }
        }
    }
    // WM_CHAR is deliberately not handled. The character is synthesised from
    // WM_INPUT instead, because the game's message loop does not call
    // TranslateMessage for us and so this branch could never fire. Handling
    // both would insert every character twice on any loader that does pump
    // WM_CHAR.
    //
    // If the player tabs out/in with UI visible, this WndProc doesn't run during mouse or keyboard events.
    // When player tabs in, force the UI state
    else if (uMsg == WM_SETFOCUS && s_pOverlay->GetActive())
    {
        InputService::SetCaptureEnabled(true);
        s_pOverlay->SetActive(true);
        pRenderer->SetCursorVisible(true);
    }
    else if (uMsg == WM_IME_STARTCOMPOSITION)
    {
        s_imeComposing = true;
    }
    else if (uMsg == WM_IME_ENDCOMPOSITION)
    {
        s_imeComposing = false;
    }
    else if (uMsg == WM_IME_COMPOSITION)
    {
        // The finished result of a composition is the only part this subclass
        // forwards as text. The intermediate composition string is left to the
        // renderer's own IME handling; trying to mirror it here would fight
        // whatever Chromium already drew and duplicate the final commit.
        if (lParam & GCS_RESULTSTR)
        {
            const HIMC hInputContext = ::ImmGetContext(hwnd);
            if (hInputContext)
            {
                const LONG cBytes = ::ImmGetCompositionStringW(hInputContext, GCS_RESULTSTR, nullptr, 0);
                if (cBytes > 0)
                {
                    std::wstring text(static_cast<size_t>(cBytes) / sizeof(wchar_t), L'\0');
                    const LONG cRead = ::ImmGetCompositionStringW(hInputContext, GCS_RESULTSTR, text.data(), cBytes);
                    text.resize(static_cast<size_t>(cRead) / sizeof(wchar_t));

                    ProcessImeResult(text);
                }

                ::ImmReleaseContext(hwnd, hInputContext);
            }
        }
    }
    else if (uMsg == WM_INPUTLANGCHANGE)
    {
        s_currentACP = GetRealACP();
        spdlog::info("Input language changed, current ACP: {}", s_currentACP);
    }

    return 0;
}

InputService::InputService(OverlayService& aOverlay) noexcept
{
    s_pOverlay = &aOverlay;
    s_currentACP = GetRealACP();
}

InputService::~InputService() noexcept
{
    s_pOverlay = nullptr;
}
