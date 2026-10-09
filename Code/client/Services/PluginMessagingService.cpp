#include <World.h>

#include <Services/PluginMessagingService.h>
#include <Services/TransportService.h>

#include <Messages/PluginMessagingRequest.h>

#include <Components.h>

#include <spdlog/spdlog.h>

#include <cstring>
#include <mutex>

namespace
{
// The registry is touched from the game thread (plugin calls) and from the
// network thread (incoming payloads), so every access is locked. Callbacks are
// collected under the lock and invoked after it is released: a plugin that
// sends from inside its own receive handler would otherwise deadlock on a
// non-recursive mutex.
std::mutex g_pluginMessagingMutex;
}

PluginMessagingService& PluginMessagingService::Get() noexcept
{
    static PluginMessagingService s_instance;
    return s_instance;
}

void PluginMessagingService::Initialize(World& aWorld) noexcept
{
    m_pWorld = &aWorld;

    // A peer's proxy is the FormIdComponent of the entity that carries its
    // PlayerComponent. Registering here is what makes the mapping events below
    // possible at all - without it a plugin that asked to be told about proxies
    // was never called, and had to poll ResolveProxy on every payload.
    m_playerAddedConnection = aWorld.on_construct<PlayerComponent>().connect<&PluginMessagingService::OnPlayerComponentAdded>(this);
    m_playerRemovedConnection = aWorld.on_destroy<PlayerComponent>().connect<&PluginMessagingService::OnPlayerComponentRemoved>(this);
}

void PluginMessagingService::OnPlayerComponentAdded(entt::registry& aRegistry, entt::entity aEntity) noexcept
{
    const auto* pPlayer = aRegistry.try_get<PlayerComponent>(aEntity);
    const auto* pFormId = aRegistry.try_get<FormIdComponent>(aEntity);
    if (!pPlayer || !pFormId || pFormId->Id == 0)
        return;

    FireProxyMapping(STRPM::ProxyMappingEventType::kAdded, pPlayer->Id, STRPM::kInvalidProxyFormID, pFormId->Id);
}

void PluginMessagingService::OnPlayerComponentRemoved(entt::registry& aRegistry, entt::entity aEntity) noexcept
{
    const auto* pPlayer = aRegistry.try_get<PlayerComponent>(aEntity);
    const auto* pFormId = aRegistry.try_get<FormIdComponent>(aEntity);
    if (!pPlayer || !pFormId)
        return;

    FireProxyMapping(STRPM::ProxyMappingEventType::kRemoved, pPlayer->Id, pFormId->Id, STRPM::kInvalidProxyFormID);
}

void PluginMessagingService::FireProxyMapping(STRPM::ProxyMappingEventType aType, STRPM::ConnectionID aConnectionId,
                                              STRPM::ProxyFormID aOldFormId, STRPM::ProxyFormID aNewFormId) noexcept
{
    STRPM::ProxyMappingEvent event{};
    event.type = aType;
    event.connectionID = aConnectionId;
    event.oldFormID = aOldFormId;
    event.newFormID = aNewFormId;

    // Collected under the lock and invoked after it is dropped, the same way
    // OnPluginMessage delivers to channel listeners. The copy allocates, and
    // this is reached from an entt observer - a frame with no caller that could
    // catch a throw, running once per player spawn and once per despawn.
    TiltedPhoques::Vector<MappingListener> listeners;
    try
    {
        std::scoped_lock lock(g_pluginMessagingMutex);

        for (const auto& listener : m_mappingListeners)
            listeners.push_back(MappingListener{ listener.Callback, listener.UserData });
    }
    catch (...)
    {
        Log("dropped a proxy mapping event: ran out of memory while collecting its listeners");
        return;
    }

    for (const auto& listener : listeners)
        listener.Callback(&event, listener.UserData);
}

void PluginMessagingService::Shutdown() noexcept
{
    // Released before the registry is touched: the World this points at is
    // being destroyed, and an observer that outlived it would be called through
    // a dangling pointer. entt spells this release(); the connection's own
    // destructor does the same thing, but the singleton outlives the World, so
    // it has to happen here rather than at static destruction.
    m_playerAddedConnection.release();
    m_playerRemovedConnection.release();

    std::scoped_lock lock(g_pluginMessagingMutex);
    m_listeners.clear();
    m_channels.clear();
    m_mappingListeners.clear();
    m_transportCallback = nullptr;
    m_transportUserData = nullptr;
    m_pWorld = nullptr;
}

STRPM::Result PluginMessagingService::StartTransport(STRPM::ReceiveCallback aCallback, void* apUserData) noexcept
{
    // The facade calls this with its own dispatcher and expects the callback to
    // be retained until stop(), so the payload can be handed back to it.
    std::scoped_lock lock(g_pluginMessagingMutex);
    m_transportCallback = aCallback;
    m_transportUserData = apUserData;
    return STRPM::Result::kOk;
}

STRPM::Result PluginMessagingService::StopTransport() noexcept
{
    std::scoped_lock lock(g_pluginMessagingMutex);
    m_transportCallback = nullptr;
    m_transportUserData = nullptr;
    return STRPM::Result::kOk;
}

STRPM::Result PluginMessagingService::RegisterChannel(const char* acpChannel, STRPM::ReceiveCallback aCallback, void* apUserData, STRPM::ListenerHandle* apOutHandle) noexcept
{
    if (!acpChannel || !aCallback)
        return STRPM::Result::kInvalidArgument;

    const auto length = std::strlen(acpChannel);
    if (length == 0 || length > STRPM::kMaxChannelLength)
        return STRPM::Result::kInvalidArgument;

    std::uint64_t handle = 0;

    try
    {
        std::scoped_lock lock(g_pluginMessagingMutex);

        for (const auto& listener : m_listeners)
        {
            if (listener.Channel == acpChannel)
                return STRPM::Result::kChannelAlreadyRegistered;
        }

        handle = m_nextHandle++;

        // Both rows allocate: the listener owns a copy of the channel name and
        // the handle table owns another.
        m_channels[handle] = acpChannel;
        m_listeners.push_back(Listener{ acpChannel, aCallback, apUserData });

        if (apOutHandle)
            apOutHandle->value = handle;
    }
    catch (...)
    {
        // The registration never completed, so nothing may be left of it. A row
        // without a listener would be invisible - nothing enumerates the handle
        // table - and it would hold a channel name forever.
        {
            std::scoped_lock lock(g_pluginMessagingMutex);
            m_channels.erase(handle);
        }

        Log("a plugin channel registration ran out of memory");
        return STRPM::Result::kNotAvailable;
    }

    return STRPM::Result::kOk;
}

STRPM::Result PluginMessagingService::UnregisterChannel(STRPM::ListenerHandle aHandle) noexcept
{
    try
    {
        std::scoped_lock lock(g_pluginMessagingMutex);

        const auto it = m_channels.find(aHandle.value);
        if (it == m_channels.end())
            return STRPM::Result::kChannelNotRegistered;

        // The name is copied out because erasing the listener below invalidates
        // the reference, and that copy allocates.
        const auto channel = it->second;
        for (auto listener = m_listeners.begin(); listener != m_listeners.end(); ++listener)
        {
            if (listener->Channel == channel)
            {
                m_listeners.erase(listener);
                break;
            }
        }

        m_channels.erase(it);
    }
    catch (...)
    {
        // Nothing had been erased yet, so the registration is still whole and
        // the plugin can simply try again.
        Log("a plugin channel unregistration ran out of memory");
        return STRPM::Result::kNotAvailable;
    }

    return STRPM::Result::kOk;
}

STRPM::Result PluginMessagingService::Send(const char* acpChannel, STRPM::Target aTarget, const void* apData, std::size_t aSize, std::uint32_t aFlags) noexcept
{
    if (!acpChannel || (aSize > 0 && !apData))
        return STRPM::Result::kInvalidArgument;

    const auto length = std::strlen(acpChannel);
    if (length == 0 || length > STRPM::kMaxChannelLength)
        return STRPM::Result::kInvalidArgument;

    if (aSize > STRPM::kMaxPayloadBytes)
        return STRPM::Result::kPayloadTooLarge;

    if (!m_pWorld)
        return STRPM::Result::kNotAvailable;

    auto& transport = m_pWorld->GetTransport();
    if (!transport.IsOnline())
        return STRPM::Result::kNotConnected;

    // The channel name and the payload are both copied into the request, and
    // both copies allocate. A plugin that gets a failure back keeps its own
    // copy of whatever it wanted to send, so a refused send loses nothing.
    try
    {
        PluginMessagingRequest request{};
        request.Channel = acpChannel;

        switch (aTarget.kind)
        {
        case STRPM::TargetKind::kServer:
            request.TargetKind = PluginMessagingRequest::Target::kServer;
            break;
        case STRPM::TargetKind::kHost:
            request.TargetKind = PluginMessagingRequest::Target::kHost;
            break;
        case STRPM::TargetKind::kPlayer:
            request.TargetKind = PluginMessagingRequest::Target::kPlayer;
            // The plugin echoes back the identity it was handed for a sender, which
            // is the framework's PlayerId.
            request.TargetPlayerId = static_cast<std::uint32_t>(aTarget.connectionID & 0xFFFFFFFFu);
            break;
        case STRPM::TargetKind::kAllPlayers:
        default:
            request.TargetKind = PluginMessagingRequest::Target::kAllPlayers;
            break;
        }

        if (aSize > 0)
        {
            const auto* pBytes = static_cast<const std::uint8_t*>(apData);
            request.PluginData.assign(pBytes, pBytes + aSize);
        }

        // The framework's own transport carries the payload: no separate socket and
        // no chat envelope, so nothing here can surface as a chat line.
        if (!transport.Send(request))
            return STRPM::Result::kTransportError;
    }
    catch (...)
    {
        Log("a plugin send ran out of memory while building the payload");
        return STRPM::Result::kTransportError;
    }

    return STRPM::Result::kOk;
}

STRPM::Result PluginMessagingService::GetLocalConnectionId(STRPM::ConnectionID* apOutConnectionId) noexcept
{
    if (!apOutConnectionId)
        return STRPM::Result::kInvalidArgument;

    if (!m_pWorld)
        return STRPM::Result::kNotAvailable;

    auto& transport = m_pWorld->GetTransport();
    if (!transport.IsOnline())
        return STRPM::Result::kNotConnected;

    // Plugins address peers by echoing back the identity they were given for a
    // sender, and receive their own the same way. That identity is the
    // framework's PlayerId; the server's transport handle is never exposed.
    *apOutConnectionId = static_cast<STRPM::ConnectionID>(transport.GetLocalPlayerId());
    return STRPM::Result::kOk;
}

STRPM::Result PluginMessagingService::SetLocalDisplayName(const char* acpDisplayName) noexcept
{
    if (!acpDisplayName)
        return STRPM::Result::kInvalidArgument;

    // Accepted and remembered, but deliberately not put on the wire: the name
    // other players see for a sender is the one the server authenticated at
    // login (NotifyPluginMessaging::SenderDisplayName, filled from
    // Player::GetUsername). Letting a plugin set it would let any plugin claim
    // to speak as somebody else.
    try
    {
        std::scoped_lock lock(g_pluginMessagingMutex);
        m_localDisplayName = acpDisplayName;
    }
    catch (...)
    {
        // The name is deliberately never put on the wire (see above), so losing
        // it costs nothing beyond the plugin's own bookkeeping.
        Log("a plugin display name was dropped: ran out of memory");
        return STRPM::Result::kNotAvailable;
    }

    return STRPM::Result::kOk;
}

void PluginMessagingService::SetLogCallback(STRPM::LogCallback aCallback, void* apUserData) noexcept
{
    std::scoped_lock lock(g_pluginMessagingMutex);
    m_logCallback = aCallback;
    m_logUserData = apUserData;
}

void PluginMessagingService::Log(const char* acpMessage) noexcept
{
    if (!acpMessage)
        return;

    // Copied out under the lock and called after it is dropped: the callback is
    // plugin code, and a plugin that logs from inside it would otherwise
    // deadlock on a non-recursive mutex.
    STRPM::LogCallback callback = nullptr;
    void* pUserData = nullptr;
    {
        std::scoped_lock lock(g_pluginMessagingMutex);
        callback = m_logCallback;
        pUserData = m_logUserData;
    }

    if (callback)
        callback(acpMessage, pUserData);
}


STRPM::Result PluginMessagingService::ResolveProxy(STRPM::ConnectionID aConnectionId, STRPM::ProxyFormID* apOutFormId) noexcept
{
    if (!apOutFormId)
        return STRPM::Result::kInvalidArgument;

    if (aConnectionId == 0)
        return STRPM::Result::kInvalidArgument;

    if (!m_pWorld)
        return STRPM::Result::kNotAvailable;

    // A peer is addressed by the framework's PlayerId. The entity that carries
    // that id is the remote player's local representation, and its FormIdComponent
    // is the proxy a plugin has to act on.
    const auto playerId = static_cast<std::uint32_t>(aConnectionId & 0xFFFFFFFFu);

    // view<...>() creates a component pool the first time it is asked for one,
    // and this resolver is called for every payload every plugin receives. A
    // failure here answers "not found" below, which every caller already treats
    // as a normal state rather than a failure.
    try
    {
        auto view = m_pWorld->view<FormIdComponent, PlayerComponent>();
        for (auto entity : view)
        {
            if (view.get<PlayerComponent>(entity).Id != playerId)
                continue;

            const auto formId = view.get<FormIdComponent>(entity).Id;
            if (formId == 0)
                break;

            *apOutFormId = static_cast<STRPM::ProxyFormID>(formId);
            return STRPM::Result::kOk;
        }
    }
    catch (...)
    {
        Log("a proxy lookup ran out of memory while scanning the player components");
    }

    // Not found is a normal state, not a failure: a peer that has not been
    // assigned a character yet, or has just left, genuinely has no proxy.
    *apOutFormId = STRPM::kInvalidProxyFormID;
    return STRPM::Result::kTargetNotFound;
}

STRPM::Result PluginMessagingService::RegisterProxyMappingListener(STRPM::ProxyMappingCallback aCallback, void* apUserData) noexcept
{
    if (!aCallback)
        return STRPM::Result::kInvalidArgument;

    try
    {
        std::scoped_lock lock(g_pluginMessagingMutex);

        for (const auto& listener : m_mappingListeners)
        {
            if (listener.Callback == aCallback && listener.UserData == apUserData)
                return STRPM::Result::kOk;
        }

        m_mappingListeners.push_back(MappingListener{ aCallback, apUserData });
    }
    catch (...)
    {
        // A plugin that is not registered here is back to polling ResolveProxy,
        // which is what every plugin did before this listener existed.
        Log("a proxy mapping listener registration ran out of memory");
        return STRPM::Result::kNotAvailable;
    }

    return STRPM::Result::kOk;
}

STRPM::Result PluginMessagingService::UnregisterProxyMappingListener(STRPM::ProxyMappingCallback aCallback, void* apUserData) noexcept
{
    std::scoped_lock lock(g_pluginMessagingMutex);

    for (auto listener = m_mappingListeners.begin(); listener != m_mappingListeners.end(); ++listener)
    {
        if (listener->Callback == aCallback && listener->UserData == apUserData)
        {
            m_mappingListeners.erase(listener);
            return STRPM::Result::kOk;
        }
    }

    return STRPM::Result::kInvalidArgument;
}

void PluginMessagingService::OnPluginMessage(const NotifyPluginMessaging& acMessage) noexcept
{
    if (acMessage.Channel.empty() || acMessage.Channel.size() > STRPM::kMaxChannelLength)
    {
        Log("dropped a plugin payload with an empty or over-long channel");
        return;
    }

    if (acMessage.PluginData.size() > STRPM::kMaxPayloadBytes)
    {
        Log("dropped a plugin payload larger than the contract allows");
        return;
    }

    STRPM::Sender sender{};
    sender.connectionID = acMessage.SenderPlayerId;
    sender.displayName = acMessage.SenderDisplayName.c_str();
    sender.isHost = acMessage.SenderIsHost;

    STRPM::Message message{};
    message.channel = acMessage.Channel.c_str();
    message.data = acMessage.PluginData.data();
    message.size = acMessage.PluginData.size();
    message.sender = sender;
    message.flags = STRPM::kMessageNone;
    message.sequence = 0;

    TiltedPhoques::Vector<Delivery> deliveries;
    try
    {
        std::scoped_lock lock(g_pluginMessagingMutex);

        // A message is offered to every registration for the channel. A plugin
        // uses either the framework entry point or the facade, and only the path
        // it chose holds a registration, so this cannot double-deliver to one.
        for (const auto& listener : m_listeners)
        {
            if (listener.Channel == acMessage.Channel)
                deliveries.push_back(Delivery{ listener.Callback, listener.UserData });
        }

        if (m_transportCallback)
            deliveries.push_back(Delivery{ m_transportCallback, m_transportUserData });
    }
    catch (...)
    {
        // Same answer as the two drops above: the payload is lost, and nothing
        // half-built is handed to a plugin.
        Log("dropped a plugin payload: ran out of memory while collecting its listeners");
        return;
    }

    for (const auto& delivery : deliveries)
        delivery.Callback(&message, delivery.UserData);
}