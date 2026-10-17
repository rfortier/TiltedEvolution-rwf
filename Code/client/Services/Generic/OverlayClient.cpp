#include <TiltedOnlinePCH.h>

#include <OverlayRenderHandler.hpp>
#include <DInputHook.hpp>

#include <Services/OverlayClient.h>
#include <Services/InputService.h>
#include <Services/TransportService.h>

#include <Messages/SendChatMessageRequest.h>
#include <Messages/TeleportRequest.h>

#include <Events/SetTimeCommandEvent.h>

#include <World.h>

OverlayClient::OverlayClient(TransportService& aTransport, TiltedPhoques::OverlayRenderHandler* apHandler)
    : TiltedPhoques::OverlayClient(apHandler)
    , m_transport(aTransport)
{
}

OverlayClient::~OverlayClient() noexcept
{
}

bool OverlayClient::OnProcessMessageReceived(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, CefProcessId source_process, CefRefPtr<CefProcessMessage> message)
{
    if (message->GetName() == "ui-event")
    {
        auto pArguments = message->GetArgumentList();

        auto eventName = pArguments->GetString(0).ToString();
        auto eventArgs = pArguments->GetList(1);

        spdlog::info(eventName);
        spdlog::info(eventArgs->GetString(0).ToString());
        spdlog::info(std::to_string(eventArgs->GetInt(1)));
        spdlog::info(eventArgs->GetString(2).ToString());

#ifndef PUBLIC_BUILD
        LOG(INFO) << "event=ui_event name=" << eventName;
#endif

        if (eventName == "connect")
            ProcessConnectMessage(eventArgs);
        else if (eventName == "disconnect")
            ProcessDisconnectMessage();
        else if (eventName == "abandonAttempt")
            ProcessAbandonAttemptMessage();
        else if (eventName == "revealPlayers")
            ProcessRevealPlayersMessage();
        else if (eventName == "sendMessage")
            ProcessChatMessage(eventArgs);
        else if (eventName == "setTime")
            ProcessSetTimeCommand(eventArgs);
        else if (eventName == "launchParty")
            World::Get().GetRunner().Queue([]() { World::Get().GetPartyService().CreateParty(); });
        else if (eventName == "leaveParty")
            World::Get().GetRunner().Queue([]() { World::Get().GetPartyService().LeaveParty(); });
        else if (eventName == "createPartyInvite")
        {
            const uint32_t aPlayerId = eventArgs->GetInt(0);
            World::Get().GetRunner().Queue([aPlayerId]() { World::Get().GetPartyService().CreateInvite(aPlayerId); });
        }
        else if (eventName == "acceptPartyInvite")
        {
            uint32_t aInviterId = eventArgs->GetInt(0);
            // push to main thread because the party service has to check validity of invite thread safely
            World::Get().GetRunner().Queue([aInviterId]() { World::Get().GetPartyService().AcceptInvite(aInviterId); });
        }
        else if (eventName == "kickPartyMember")
        {
            const uint32_t aPlayerId = eventArgs->GetInt(0);
            World::Get().GetRunner().Queue([aPlayerId]() { World::Get().GetPartyService().KickPartyMember(aPlayerId); });
        }
        else if (eventName == "changePartyLeader")
        {
            const uint32_t aPlayerId = eventArgs->GetInt(0);
            World::Get().GetRunner().Queue([aPlayerId]() { World::Get().GetPartyService().ChangePartyLeader(aPlayerId); });
        }
        else if (eventName == "teleportToPlayer")
            ProcessTeleportMessage(eventArgs);
        else if (eventName == "toggleDebugUI")
            ProcessToggleDebugUI();
        else if (eventName == "openPlayGuide")
            ProcessOpenPlayGuide();
        else if (eventName == "deactivate")
            ProcessDeactivateMessage();

        return true;
    }

    return false;
}

void OverlayClient::ProcessConnectMessage(CefRefPtr<CefListValue> aEventArgs)
{
    std::string baseIp = aEventArgs->GetString(0);
    if (baseIp == "localhost")
    {
        baseIp = "127.0.0.1";
    }

    uint16_t port = aEventArgs->GetInt(1) ? aEventArgs->GetInt(1) : 10578;
    World::Get().GetTransport().SetServerPassword(aEventArgs->GetString(2));

    std::string endpoint = baseIp + ":" + std::to_string(port);

    // Close(), ArmConnectionWatchdog() and BeginConnect() only touch the uv
    // loop, the socket interface and plain scalar members, and this method
    // already runs on the CEF message thread, which is where every other
    // ui-event is handled. Running them inline takes the start of the attempt
    // off the frame loop, so opening the connection panel -- which is what
    // precedes those stalls -- can no longer swallow it.
    //
    // BeginConnect goes further: it hands the attempt a pump of its own, so the
    // handshake no longer depends on the vm tick arriving at all. A tick-driven
    // attempt cannot even report that the tick is missing, which is how a
    // stalled connection has been able to sit there printing nothing.
    auto& transport = World::Get().GetTransport();
    // A second connect while one is still in flight used to stack a new
    // transport on top of the old one and leak its handle.
    transport.Close();
    transport.ArmConnectionWatchdog(endpoint);
    transport.BeginConnect(endpoint);
}

void OverlayClient::ProcessDisconnectMessage()
{
    // Same reasoning as the connect above: cancelling must work even when the
    // runner is not being drained, otherwise the button appears to do nothing
    // and the attempt is left running with no way to stop it.
    World::Get().GetTransport().Close();
}

void OverlayClient::ProcessAbandonAttemptMessage()
{
    World::Get().GetTransport().AbandonAttempt();
}

void OverlayClient::ProcessRevealPlayersMessage()
{
    // Closing the UI is CEF's own state and has to happen here; the reveal
    // walks every remote player and adds a magic target to each, which is game
    // state and belongs on the game thread.
    SetUIVisible(false);
    World::Get().GetRunner().Queue([]() { World::Get().GetMagicService().StartRevealingOtherPlayers(); });
}

void OverlayClient::ProcessChatMessage(CefRefPtr<CefListValue> aEventArgs)
{
    std::string contents = aEventArgs->GetString(1).ToString();
    if (!contents.empty())
    {
        SendChatMessageRequest messageRequest;
        messageRequest.MessageType = static_cast<ChatMessageType>(aEventArgs->GetInt(0));
        messageRequest.ChatMessage = contents;

        spdlog::info(L"Send chat message of type {}: '{}' ", messageRequest.MessageType, aEventArgs->GetString(1).ToWString());

        m_transport.Send(messageRequest);
    }
}

void OverlayClient::ProcessSetTimeCommand(CefRefPtr<CefListValue> aEventArgs)
{
    const uint8_t hours = static_cast<uint8_t>(aEventArgs->GetInt(0));
    const uint8_t minutes = static_cast<uint8_t>(aEventArgs->GetInt(1));

    // The dispatcher is not thread safe and this runs on the CEF message thread.
    // Queueing also keeps the local player id read on the thread that owns it.
    World::Get().GetRunner().Queue([hours, minutes]() {
        const uint32_t senderId = World::Get().GetTransport().GetLocalPlayerId();
        World::Get().GetDispatcher().trigger(SetTimeCommandEvent(hours, minutes, senderId));
    });
}

void OverlayClient::ProcessTeleportMessage(CefRefPtr<CefListValue> aEventArgs)
{
    TeleportRequest request{};
    request.PlayerId = static_cast<uint32_t>(aEventArgs->GetInt(0));

    m_transport.Send(request);
}

void OverlayClient::ProcessToggleDebugUI()
{
    World::Get().GetRunner().Queue([]() {
        auto& debugService = World::Get().GetDebugService();
        debugService.m_showDebugStuff = !debugService.m_showDebugStuff;
    });
}

void OverlayClient::ProcessOpenPlayGuide()
{
    ShellExecuteW(nullptr, L"open", LR"(https://wiki.tiltedphoques.com/tilted-online/general-information/playguide)", nullptr, nullptr, SW_SHOWNORMAL);
}

void OverlayClient::ProcessDeactivateMessage()
{
    // The overlay object documents this call as "deactivate UI and release
    // control", and SetUIVisible is that pair: it releases the input capture, tells
    // the page it is no longer active and hides the cursor. It has to be the same
    // pair the game side performs when it hides the overlay itself, or the two ways
    // of closing the overlay would disagree about the capture state. Called
    // directly from this handler like ProcessRevealPlayersMessage above does.
    //
    // It used to be registered in ProcessHandler with no branch here, so the UI's
    // request crossed the process boundary and the if-chain dropped it: no error,
    // no effect. Tools/Scripts/check_overlay_bridge.py now requires every
    // registered name the UI calls to have its branch.
    SetUIVisible(false);
}

void OverlayClient::SetUIVisible(bool aVisible) noexcept
{
    auto pRenderer = GetOverlayRenderHandler();
    if (!pRenderer)
        return;

    InputService::SetCaptureEnabled(aVisible);
    World::Get().GetOverlayService().SetActive(aVisible);
    pRenderer->SetCursorVisible(aVisible);
}
