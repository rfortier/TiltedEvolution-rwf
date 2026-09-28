
#include <Services/TransportService.h>

#include <Events/ConnectedEvent.h>
#include <Events/ConnectionErrorEvent.h>
#include <Events/DisconnectedEvent.h>
#include <Events/UpdateEvent.h>

#include <Games/References.h>
#include <Games/TES.h>
#include <Forms/TESWorldSpace.h>
#include <Forms/TESObjectCELL.h>

#include <TimeManager.h>

#include <Forms/TESNPC.h>
#include <TiltedOnlinePCH.h>
#include <World.h>

#include <Messages/AuthenticationRequest.h>
#include <Messages/ServerMessageFactory.h>
#include <Messages/NotifySettingsChange.h>
#include <Packet.hpp>

#include <ScriptExtender.h>
#include <Services/DiscordService.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <thread>

// #include <imgui_internal.h>

// How long the handshake pump sleeps between Update() calls. The Steam side
// wakes on its own socket activity, so this only decides how quickly the
// watchdog can notice the deadline and how much of one core an attempt burns.
static constexpr auto kHandshakePumpInterval = std::chrono::milliseconds(8);

// The pump announces itself this often, which is what lets a stalled attempt be
// told apart from a dead frame loop in the log alone.
static constexpr auto kPumpHeartbeat = std::chrono::seconds(5);

// How long the frame loop may go silent before its tick counts as missing. Well
// past any frame even on a struggling machine, far short of anything a player
// would notice.
static constexpr uint64_t kGameThreadStallMs = 1000;

// How long the handshake pump waits for the game thread to come and take a
// parsed message before it gives up waiting and dispatches it itself. One frame
// would be plenty on a healthy client; this leaves room for a loading hiccup
// without letting an attempt stall on a queue nobody drains.
static constexpr auto kHandoffTimeout = std::chrono::milliseconds(500);

static uint64_t NowMs() noexcept
{
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch())
            .count());
}

// Which Steam connection state the transport reports ticked by. Printed as a
// number because the enum name of a not-yet-open connection is not informative
// enough to be worth listing here.
static const char* DescribeConnectionState(int aState) noexcept
{
    switch (aState)
    {
    case k_ESteamNetworkingConnectionState_None: return "none";
    case k_ESteamNetworkingConnectionState_Connecting: return "connecting";
    case k_ESteamNetworkingConnectionState_FindingRoute: return "finding route";
    case k_ESteamNetworkingConnectionState_Connected: return "connected";
    case k_ESteamNetworkingConnectionState_ClosedByPeer: return "closed by peer";
    case k_ESteamNetworkingConnectionState_ProblemDetectedLocally: return "problem detected locally";
    default: return "other";
    }
}

static constexpr wchar_t kMO2DllName[] = L"usvfs_x64.dll";

// How long the client waits for the whole handshake (name resolution, transport
// connect, clock sync, authentication response) before giving up and telling
// the UI. A server on the local network answers in well under a second; ten
// seconds is generous enough that a slow VPN does not trip it, and short enough
// that a wrong address does not look like a freeze.
static constexpr std::chrono::seconds kHandshakeTimeout{10};

using TiltedPhoques::Packet;

TransportService::TransportService(World& aWorld, entt::dispatcher& aDispatcher) noexcept
    : m_world(aWorld)
    , m_dispatcher(aDispatcher)
{
    m_updateConnection = m_dispatcher.sink<UpdateEvent>().connect<&TransportService::HandleUpdate>(this);
    m_settingsChangeConnection = m_dispatcher.sink<NotifySettingsChange>().connect<&TransportService::HandleNotifySettingsChange>(this);
    m_connectedConnection = m_dispatcher.sink<ConnectedEvent>().connect<&TransportService::HandleConnected>(this);
    m_disconnectedConnection = m_dispatcher.sink<DisconnectedEvent>().connect<&TransportService::HandleDisconnected>(this);

    m_connected = false;
    m_localPlayerId = NULL;

    // Whoever builds the client is the thread the game runs on: World is
    // constructed there, and it is the one every service in this dispatcher was
    // written against. It is also the only thread that has ever dispatched a
    // message up to now, so letting it keep doing so changes nothing for a
    // client whose tick is healthy.
    m_gameThreadId = std::this_thread::get_id();

    auto handlerGenerator = [this](auto& x)
    {
        using T = typename std::remove_reference_t<decltype(x)>::Type;

        m_messageHandlers[T::Opcode] = [this](UniquePtr<ServerMessage>& apMessage)
        {
            const auto pRealMessage = TiltedPhoques::CastUnique<T>(std::move(apMessage));
            m_dispatcher.trigger(*pRealMessage);
        };

        return false;
    };

    ServerMessageFactory::Visit(handlerGenerator);

    // Override authentication response
    m_messageHandlers[AuthenticationResponse::Opcode] = [this](UniquePtr<ServerMessage>& apMessage)
    {
        const auto pRealMessage = TiltedPhoques::CastUnique<AuthenticationResponse>(std::move(apMessage));
        HandleAuthenticationResponse(*pRealMessage);
    };
}

TransportService::~TransportService() noexcept
{
    // Nothing may be mid-Update() once World is being torn down, otherwise the
    // pump would be calling into services that are already half destroyed.
    StopHandshakePump();
}

// Named to hide Client::Close() for every caller that goes through this type,
// which is how the lock below ends up covering call sites that were written
// long before the transport had more than one thread to worry about.
void TransportService::Close() noexcept
{
    std::lock_guard<std::recursive_mutex> lock(m_clientMutex);

    Client::Close();
}

bool TransportService::Send(const ClientMessage& acMessage) const noexcept
{
    std::lock_guard<std::recursive_mutex> lock(m_clientMutex);

    static thread_local ScratchAllocator s_allocator(1 << 18);

    struct ScopedReset
    {
        ~ScopedReset() { s_allocator.Reset(); }
    } allocatorGuard;

    if (IsConnected())
    {
        ScopedAllocator _{s_allocator};

        Buffer buffer(1 << 16);
        Buffer::Writer writer(&buffer);
        writer.WriteBits(0, 8); // Write first byte as packet needs it

        acMessage.Serialize(writer);
        TiltedPhoques::PacketView packet(reinterpret_cast<char*>(buffer.GetWriteData()), writer.Size());

        Client::Send(&packet);

        return true;
    }

    return false;
}

void TransportService::OnConsume(const void* apData, uint32_t aSize)
{
    ServerMessageFactory factory;
    TiltedPhoques::ViewBuffer buf((uint8_t*)apData, aSize);
    Buffer::Reader reader(&buf);

    auto pMessage = factory.Extract(reader);
    if (!pMessage)
    {
        spdlog::error("Couldn't parse packet from server");
        return;
    }

    // Parsing happened here and is done; what remains belongs to the game
    // thread. Everything downstream of a dispatch - the authentication
    // response most of all, which turns into a ConnectedEvent and has the
    // services walk the world - reads and writes game state that this thread
    // has no business touching while the game is running.
    if (std::this_thread::get_id() == m_gameThreadId)
    {
        DispatchMessage(pMessage);
        return;
    }

    std::lock_guard<std::mutex> lock(m_pendingMutex);
    m_pendingMessages.push_back(std::move(pMessage));
}

void TransportService::DispatchMessage(UniquePtr<ServerMessage>& apMessage) noexcept
{
    const auto cOpcode = apMessage->GetOpcode();
    if (cOpcode >= kServerOpcodeMax)
    {
        spdlog::error("Server message opcode {} is outside the handler table", static_cast<int>(cOpcode));
        return;
    }

    auto& handler = m_messageHandlers[cOpcode];
    if (!handler)
    {
        spdlog::error("No handler for server message opcode {}", static_cast<int>(cOpcode));
        return;
    }

    handler(apMessage);
}

bool TransportService::FlushPendingMessages() noexcept
{
    std::vector<UniquePtr<ServerMessage>> batch;
    {
        std::lock_guard<std::mutex> lock(m_pendingMutex);
        if (m_pendingMessages.empty())
            return false;

        batch.swap(m_pendingMessages);
    }

    for (auto& message : batch)
    {
        if (message)
            DispatchMessage(message);
    }

    return true;
}

bool TransportService::HasPendingMessages() const noexcept
{
    std::lock_guard<std::mutex> lock(m_pendingMutex);

    return !m_pendingMessages.empty();
}

void TransportService::ArmConnectionWatchdog(const std::string& acEndpoint) noexcept
{
    m_handshakePending = true;
    m_handshakeDeadline = std::chrono::steady_clock::now() + kHandshakeTimeout;
    m_pendingEndpoint = acEndpoint;
    m_attemptErrorReported = false;
    m_attemptDisconnectReported = false;

    spdlog::info("connecting to {}, giving up after {}s if the handshake does not complete",
                 acEndpoint, kHandshakeTimeout.count());
}

void TransportService::OnConnected()
{
    AuthenticationRequest request{};
    request.Version = BUILD_COMMIT;
    request.SKSEActive = IsScriptExtenderLoaded();
    request.MO2Active = GetModuleHandleW(kMO2DllName);

    request.Token = m_serverPassword;
    m_serverPassword = "";

    PlayerCharacter* pPlayer = PlayerCharacter::Get();

    // null if discord is not active
    // TODO: think about user opt out
    request.DiscordId = m_world.ctx().at<DiscordService>().GetUser().id;
    auto* pNpc = Cast<TESNPC>(pPlayer->baseForm);
    if (pNpc)
    {
        request.Username = pNpc->fullName.value.AsAscii();
    }
    else
    {
        request.Username = "Some dragon boi";
    }

    auto* const cpModManager = ModManager::Get();

    for (auto* pMod : cpModManager->mods)
    {
        if (!pMod->IsLoaded())
            continue;

        auto& entry = request.UserMods.ModList.emplace_back();
        entry.Id = pMod->GetId();
        entry.IsLite = pMod->IsLite();
        entry.Filename = pMod->filename;
    }

    auto& modSystem = m_world.GetModSystem();
    if (pPlayer->GetWorldSpace())
        modSystem.GetServerModId(pPlayer->GetWorldSpace()->formID, request.WorldSpaceId);

    modSystem.GetServerModId(pPlayer->parentCell->formID, request.CellId);

    request.Level = pPlayer->GetLevel();

    auto* pGameTime = TimeData::Get();
    request.PlayerTime.TimeScale = pGameTime->TimeScale->f;
    request.PlayerTime.Time = pGameTime->GameHour->f;
    request.PlayerTime.Year = pGameTime->GameYear->f;
    request.PlayerTime.Month = pGameTime->GameMonth->f;
    request.PlayerTime.Day = pGameTime->GameDay->f;

    // The transport is up and the clock is synced; what remains is the server's
    // answer to this request, so the watchdog now covers the auth round trip.
    spdlog::info("transport connected, authentication request sent");

    Send(request);
}

void TransportService::OnDisconnected(EDisconnectReason aReason)
{
    // Read before touching: a teardown can re-enter here, and the second entry
    // has to see whether an attempt was still in flight when the first ran.
    //
    // m_handshakePending is also deliberately cleared *after* the Close() that
    // may have brought us here (see the timeout branch in HandleUpdate and the
    // rejection branch at the end of HandleAuthenticationResponse). Those paths
    // raise an error event first and then tear the transport down, and this flag
    // being still set is what makes that teardown count as part of this attempt
    // rather than as the tail end of one that already ended.
    const bool cWasAttempting = m_connected || m_handshakePending;

    m_connected = false;
    m_handshakePending = false;

    // One attempt can report its teardown more than once: Close() reports
    // kAborted synchronously for the transport, and uv_cancel wakes the name
    // resolution callback, which reports kAborted again on the next pump. Only
    // the first of those may raise DisconnectedEvent.
    //
    // Note what is *not* covered by this dedup: a failure raises
    // ConnectionErrorEvent and only then tears the transport down, so its
    // teardown still arrives here with m_attemptDisconnectReported unset and
    // does report the disconnect. That is deliberate. The disconnect is the
    // signal every service uses to drop its session state, so it has to happen
    // on failure paths too rather than being left to the error handler.
    if (m_attemptDisconnectReported)
    {
        spdlog::info("disconnect already reported for this attempt, ignoring the duplicate ({})",
                     static_cast<int>(aReason));
        return;
    }
    m_attemptDisconnectReported = true;

    static constexpr const char* kReasonNames[] = {"timeout",    "local problem", "kicked",
                                                   "cannot resolve address", "aborted", "normal"};
    constexpr auto kReasonCount = static_cast<int>(std::size(kReasonNames));
    const auto cReasonIndex = static_cast<int>(aReason);
    const auto cReasonName =
        cReasonIndex >= 0 && cReasonIndex < kReasonCount ? kReasonNames[cReasonIndex] : "unknown";

    if (!cWasAttempting)
    {
        // Reached for a teardown that was not part of an attempt: Close() calls
        // into the transport even when there is nothing to close, and the
        // reconnect path tears down deliberately without intending to report.
        // Re-triggering DisconnectedEvent here would have every service reset
        // itself a second time for no reason.
        spdlog::debug("Disconnected from server with no attempt in flight ({}): {}", cReasonIndex,
                      cReasonName);
        return;
    }

    spdlog::warn("Disconnected from server ({}): {}", cReasonIndex, cReasonName);

    // Carry the failure through: the overlay needs it to tell a failed attempt
    // apart from a session that had been running and simply ended, which is what
    // decides whether it offers to reconnect.
    m_dispatcher.trigger(DisconnectedEvent(m_attemptErrorReported.load()));
}

void TransportService::OnUpdate()
{
}

void TransportService::AbandonAttempt() noexcept
{
    // Clears the in-flight marker first so the teardown below has nothing to
    // report, and marks the disconnect reported as well so the teardown cannot
    // slip through the other channel either. Only the reconnect path uses this:
    // it is about to start a replacement attempt, and telling the UI the old
    // one ended would be worse than noise -- it would drop the player out of
    // "connecting" between the two tries.
    m_handshakePending = false;
    m_attemptDisconnectReported = true;
    Close();
}

void TransportService::PumpHeartbeat() noexcept
{
    const auto cNow = std::chrono::steady_clock::now();
    if (m_lastPumpHeartbeat.time_since_epoch().count() != 0 && cNow - m_lastPumpHeartbeat < kPumpHeartbeat)
        return;

    m_lastPumpHeartbeat = cNow;

    spdlog::info("transport pump heartbeat: update events {}, handshake pending {}, connected {}, owned by handshake "
                 "pump {}",
                 m_pumpTicks.load(), m_handshakePending.load(), m_connected.load(), m_handshakeThreadActive.load());
}

bool TransportService::PumpConnectionLocked() noexcept
{
    // The watchdog moved out of HandleUpdate with the rest of this function so
    // that whichever thread pumps the transport also enforces the deadline. A
    // watchdog that only ticks with the frame loop cannot notice that the frame
    // loop has stopped.
    if (m_handshakePending && !m_attemptErrorReported &&
        std::chrono::steady_clock::now() >= m_handshakeDeadline)
    {
        spdlog::error("Handshake with {} timed out after {}s, aborting", m_pendingEndpoint,
                      kHandshakeTimeout.count());

        // This attempt is over, so state the failure once. Only the *error* is
        // marked reported here: the disconnect that Close() produces is a
        // separate channel and still has to fire, because that is what makes
        // the services drop their session state.
        m_attemptErrorReported = true;

        ConnectionErrorEvent errorEvent;
        errorEvent.ErrorDetail = "{\"error\": \"no_reason\"}";
        m_dispatcher.trigger(errorEvent);

        Close();
    }

    Update();

    return m_handshakePending.load();
}

bool TransportService::IsLiteralIPV4(const std::string& acEndpoint) noexcept
{
    // The port, when there is one, follows the last colon; what precedes it has
    // to be four dot-separated groups of digits for DNS to be skippable.
    // Anything else - a name, and the bracketless form of an IPv6 literal
    // included - still goes through getaddrinfo, which now runs on the same
    // thread that owns the loop, so it is merely slower and not unsound.
    const std::string cHost = acEndpoint.substr(0, acEndpoint.find_last_of(':'));

    if (std::count(cHost.begin(), cHost.end(), '.') != 3)
        return false;

    return std::all_of(cHost.begin(), cHost.end(),
                       [](char aCharacter)
                       {
                           return aCharacter == '.' ||
                                  std::isdigit(static_cast<unsigned char>(aCharacter)) != 0;
                       });
}

void TransportService::BeginConnect(const std::string& acEndpoint) noexcept
{
    // Two pumps would put two threads inside Client at once. Only ever one.
    StopHandshakePump();

    m_handshakeThreadStop = false;
    m_handshakeThreadActive = true;

    m_handshakeThread = std::thread([this, acEndpoint]() { HandshakeThreadMain(acEndpoint); });
}

void TransportService::StopHandshakePump() noexcept
{
    m_handshakeThreadStop = true;

    if (m_handshakeThread.joinable())
        m_handshakeThread.join();

    m_handshakeThreadActive = false;
}

void TransportService::HandshakeThreadMain(const std::string acEndpoint) noexcept
{
    {
        std::lock_guard<std::recursive_mutex> lock(m_clientMutex);

        // A literal address needs no name resolution, and skipping it removes
        // the one uv request the attempt used to make - uv_getaddrinfo, issued
        // from the CEF message thread against a loop the frame loop owns and
        // pumps, which is neither safe nor guaranteed to ever be delivered.
        const bool cLiteralAddress = IsLiteralIPV4(acEndpoint);

        spdlog::info("handshake pump: {} {}", (cLiteralAddress ? "dialling" : "resolving"), acEndpoint);

        const bool cStarted = cLiteralAddress ? Client::ConnectByIp(acEndpoint) : Client::Connect(acEndpoint);
        if (!cStarted)
        {
            spdlog::warn("handshake pump: {} could not be started", acEndpoint);
        }
    }

    auto cLastStateReport = std::chrono::steady_clock::now();

    while (m_handshakePending.load() && !m_handshakeThreadStop.load())
    {
        // A frame loop that reached HandleUpdate less than a second ago still
        // owns the session: whatever arrives next carries world state, and
        // handling that here instead of there is only ever justified for the
        // sessions whose tick is not coming. On those - which is what this pump
        // exists for - nothing here would ever run at all without it.
        if (NowMs() - m_lastGameThreadTickMs.load() < kGameThreadStallMs)
        {
            spdlog::info("handshake pump: frame loop is live, handing {} back to it", acEndpoint);
            break;
        }

        {
            std::lock_guard<std::recursive_mutex> lock(m_clientMutex);

            PumpConnectionLocked();

            const auto cNow = std::chrono::steady_clock::now();
            if (cNow - cLastStateReport >= kPumpHeartbeat)
            {
                cLastStateReport = cNow;

                const auto cState = static_cast<int>(GetConnectionStatus().m_eState);
                spdlog::info("handshake pump: waiting on {}, connection state {} ({}), clock synced {}", acEndpoint,
                             cState, DescribeConnectionState(cState), GetClock().IsSynchronized());
            }
        }

        // Anything the pump just parsed is handed to the game thread here. It is
        // the only correct place for it: the authentication response in
        // particular becomes a ConnectedEvent, and every service that answers
        // that walks the world.
        //
        // The wait is bounded on purpose. A frame loop that never arrives would
        // otherwise leave the attempt hanging on a queue nobody drains, which is
        // the failure this pump was built to escape - so past the deadline the
        // pump dispatches itself and says so in the log. That is the one case
        // where game state is touched off the game thread, and it is reserved
        // for clients whose tick genuinely never came.
        if (HasPendingMessages())
        {
            m_world.GetRunner().Queue([this]() { FlushPendingMessages(); });

            const auto cHandoffDeadline = std::chrono::steady_clock::now() + kHandoffTimeout;
            while (HasPendingMessages() && std::chrono::steady_clock::now() < cHandoffDeadline)
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
            }

            if (HasPendingMessages())
            {
                spdlog::warn("handshake pump: the frame loop did not take the queued messages within {}ms, so the "
                             "pump is dispatching them itself; game state is being touched off the game thread",
                             kHandoffTimeout.count());

                FlushPendingMessages();
            }
        }

        if (!m_handshakePending.load() || m_handshakeThreadStop.load())
            break;

        std::this_thread::sleep_for(kHandshakePumpInterval);
    }

    // Whatever arrived after the loop last looked still belongs to the game
    // thread. Left queued rather than dispatched here: nothing about it is
    // urgent any more, and a session that is going to live is one whose frame
    // loop is running.
    if (HasPendingMessages())
    {
        m_world.GetRunner().Queue([this]() { FlushPendingMessages(); });
    }

    // Ownership goes back to the frame loop here, which is why this thread ends
    // the moment the handshake does either way: what follows carries world
    // state, and handling that anywhere but on the game thread is a crash
    // waiting for a busy frame.
    m_handshakeThreadActive = false;

    spdlog::info("handshake pump: finished for {}, handshake still pending {}", acEndpoint,
                 m_handshakePending.load());
}

void TransportService::HandleUpdate(const UpdateEvent& acEvent) noexcept
{
    TP_UNUSED(acEvent);

    ++m_pumpTicks;

    // Stamped before the ownership test below, because the whole point of it is
    // to say 'the frame loop is here' even on frames that then hand over.
    m_lastGameThreadTickMs = NowMs();

    PumpHeartbeat();

    // While an attempt has a pump of its own, touching the transport here would
    // put a second thread inside Client. That pump does this same work, so this
    // frame is skipped rather than waited on.
    if (m_handshakeThreadActive.load())
        return;

    // Try rather than wait: a frame that cannot have the transport this instant
    // will be along in a few milliseconds, and holding the loop up for one is
    // worse than missing it.
    std::unique_lock<std::recursive_mutex> lock(m_clientMutex, std::try_to_lock);
    if (!lock.owns_lock())
        return;

    PumpConnectionLocked();
}

void TransportService::HandleConnected(const ConnectedEvent& acEvent) noexcept
{
    m_localPlayerId = acEvent.PlayerId;
}

void TransportService::HandleDisconnected(const DisconnectedEvent& acEvent) noexcept
{
    m_localPlayerId = NULL;
}

void TransportService::HandleAuthenticationResponse(const AuthenticationResponse& acMessage) noexcept
{
    // Whichever way the server answered, the handshake is over and the
    // watchdog must not fire on top of the answer.
    m_handshakePending = false;

    using AR = AuthenticationResponse::ResponseType;
    if (acMessage.Type == AR::kAccepted)
    {
        m_connected = true;
        m_attemptErrorReported = false;
        m_attemptDisconnectReported = false;

        spdlog::info("authentication accepted, joined as player {}", acMessage.PlayerId);

        m_world.SetServerSettings(acMessage.Settings);

        m_dispatcher.trigger(acMessage.UserMods);
        m_dispatcher.trigger(acMessage.Settings);
        m_dispatcher.trigger(ConnectedEvent(acMessage.PlayerId));
        return; // quit the function here.
    }

    // error finding

    TiltedPhoques::String ErrorInfo;

    ErrorInfo = "{";

    switch (acMessage.Type)
    {
    case AR::kWrongVersion:
        ErrorInfo += "\"error\": \"wrong_version\", \"data\": {";
        ErrorInfo += fmt::format("\"expectedVersion\": \"{}\", \"version\": \"{}\"", acMessage.Version, BUILD_COMMIT);
        ErrorInfo += "}";
        break;
    case AR::kModsMismatch:
    {
        ErrorInfo += "\"error\": \"mods_mismatch\", \"data\": {\"mods\": [";
        bool first = true;
        for (const auto& m : acMessage.UserMods.ModList)
        {
            if (!first)
                ErrorInfo += ",";
            ErrorInfo += fmt::format("[\"{}\",\"{}\"]", m.Filename.c_str(), m.Id);
            first = false;
        }
        ErrorInfo += "]}";
        break;
    }
    case AR::kClientModsDisallowed:
    {
        ErrorInfo += "\"error\": \"client_mods_disallowed\", \"data\": { \"mods\": [";
        if (acMessage.SKSEActive)
            ErrorInfo += "\"SKSE\"";
        if (acMessage.MO2Active)
            if (acMessage.SKSEActive)
                ErrorInfo += ",";
        ErrorInfo += "\"MO2\"";
        ErrorInfo += "]}";
        break;
    }
    case AR::kWrongPassword:
    {
        ErrorInfo += "\"error\": \"wrong_password\"";
        break;
    }
    case AR::kServerFull:
    {
        ErrorInfo += "\"error\": \"server_full\"";
        break;
    }
    default: ErrorInfo += "\"error\": \"no_reason\""; break;
    }

    ErrorInfo += "}";

    ConnectionErrorEvent errorEvent;
    if (!ErrorInfo.empty())
    {
        spdlog::error(ErrorInfo.c_str());
        errorEvent.ErrorDetail = std::move(ErrorInfo);
    }

    // The server rejected us, so this attempt is over and it reports its
    // failure once: the error carries the reason. Only the error is marked
    // reported -- the disconnect Close() raises below is a separate channel and
    // still fires, so the services tear their session state down even though
    // this attempt never got past authentication.
    m_attemptErrorReported = true;

    m_dispatcher.trigger(errorEvent);

    // Also tear the transport down here rather than waiting for the server's
    // close packet, which is not guaranteed to arrive at all.
    Close();
}

void TransportService::HandleNotifySettingsChange(const NotifySettingsChange& acMessage) noexcept
{
    m_world.SetServerSettings(acMessage.Settings);
    m_dispatcher.trigger(acMessage.Settings);
}
