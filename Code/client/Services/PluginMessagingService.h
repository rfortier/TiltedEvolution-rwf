#pragma once

// The authoritative contract lives in Code/plugins/STRPM and is shared with the
// companion plugins; the framework consumes the same header they do rather than
// keeping a private copy that could drift.
#include <STRPluginMessagingAPI/STRPluginMessagingAPI.h>

#include <Messages/NotifyPluginMessaging.h>

#include <TiltedCore/Stl.hpp>

#include <cstddef>
#include <cstdint>

struct World;

/**
 * @brief Native plugin-messaging transport for companion SKSE plugins.
 *
 * This is the in-process replacement for the binpatch bridge that the
 * standalone STRPluginMessagingAPI shipped: that bridge had to locate a private
 * logging anchor inside a packed third-party executable and hook its chat path,
 * because it had no other way to reach the session. This project owns both ends
 * of the protocol, so the payload travels as a real message and none of that
 * machinery is needed.
 *
 * Two entry paths land here and both must work:
 *
 *  - a plugin queries the framework runtime directly, registers a channel and
 *    receives through the listener registry below;
 *  - the STRPM facade loads, calls start() with its own receive callback, and
 *    dispatches to its registered channels itself.
 *
 * A message is offered to both, because a plugin uses one path or the other and
 * only the path it chose has a registration for its channel.
 */
class PluginMessagingService
{
public:
    static PluginMessagingService& Get() noexcept;

    void Initialize(World& aWorld) noexcept;
    void Shutdown() noexcept;

    // ---- transport surface, called through the exported C ABI ---------------

    [[nodiscard]] STRPM::Result StartTransport(STRPM::ReceiveCallback aCallback, void* apUserData) noexcept;
    [[nodiscard]] STRPM::Result StopTransport() noexcept;

    [[nodiscard]] STRPM::Result RegisterChannel(const char* acpChannel, STRPM::ReceiveCallback aCallback, void* apUserData, STRPM::ListenerHandle* apOutHandle) noexcept;
    [[nodiscard]] STRPM::Result UnregisterChannel(STRPM::ListenerHandle aHandle) noexcept;
    [[nodiscard]] STRPM::Result Send(const char* acpChannel, STRPM::Target aTarget, const void* apData, std::size_t aSize, std::uint32_t aFlags) noexcept;
    [[nodiscard]] STRPM::Result GetLocalConnectionId(STRPM::ConnectionID* apOutConnectionId) noexcept;
    [[nodiscard]] STRPM::Result SetLocalDisplayName(const char* acpDisplayName) noexcept;

    // Resolves a peer to the local actor that represents them. A plugin that
    // synchronises scene state needs the remote player's proxy FormID to act on
    // it, and this is the only place that mapping exists.
    [[nodiscard]] STRPM::Result ResolveProxy(STRPM::ConnectionID aConnectionId, STRPM::ProxyFormID* apOutFormId) noexcept;
    [[nodiscard]] STRPM::Result RegisterProxyMappingListener(STRPM::ProxyMappingCallback aCallback, void* apUserData) noexcept;
    [[nodiscard]] STRPM::Result UnregisterProxyMappingListener(STRPM::ProxyMappingCallback aCallback, void* apUserData) noexcept;

    // The facade owns the same pair and invokes it from its own Log(); the
    // framework does the same for the diagnostics it produces itself, so a plugin
    // that installed one is not left with a callback that never fires.
    void SetLogCallback(STRPM::LogCallback aCallback, void* apUserData) noexcept;
    void Log(const char* acpMessage) noexcept;

    // ---- framework side -----------------------------------------------------

    void OnPluginMessage(const NotifyPluginMessaging& acMessage) noexcept;

private:
    // The proxy mapping a plugin needs is the FormIdComponent of the entity that
    // carries a remote player's PlayerComponent. These two observers are the only
    // places that mapping appears and disappears, so the mapping events are fired
    // from them rather than left for the plugin to poll for.
    void OnPlayerComponentAdded(entt::registry& aRegistry, entt::entity aEntity) noexcept;
    void OnPlayerComponentRemoved(entt::registry& aRegistry, entt::entity aEntity) noexcept;

    // Delivers one mapping event to every registered listener. Collects first and
    // calls after the lock is dropped, for the same reason OnPluginMessage does: a
    // listener that calls back into the service would otherwise deadlock.
    void FireProxyMapping(STRPM::ProxyMappingEventType aType, STRPM::ConnectionID aConnectionId,
                          STRPM::ProxyFormID aOldFormId, STRPM::ProxyFormID aNewFormId) noexcept;

    PluginMessagingService() = default;
    ~PluginMessagingService() = default;

    PluginMessagingService(const PluginMessagingService&) = delete;
    PluginMessagingService& operator=(const PluginMessagingService&) = delete;

    struct Listener
    {
        TiltedPhoques::String Channel;
        STRPM::ReceiveCallback Callback{ nullptr };
        void* UserData{ nullptr };
    };

    // Delivery targets collected under the lock and invoked after it is dropped.
    struct Delivery
    {
        STRPM::ReceiveCallback Callback{ nullptr };
        void* UserData{ nullptr };
    };

    // Registered channels are few and long lived; a vector keeps delivery order
    // stable and the lookup cost is irrelevant next to a network hop.
    TiltedPhoques::Vector<Listener> m_listeners;
    TiltedPhoques::Map<std::uint64_t, TiltedPhoques::String> m_channels;

    STRPM::ReceiveCallback m_transportCallback{ nullptr };
    void* m_transportUserData{ nullptr };

    STRPM::LogCallback m_logCallback{ nullptr };
    void* m_logUserData{ nullptr };

    // Plugins that want to be told when a peer's proxy appears or disappears,
    // rather than polling for it on every payload.
    struct MappingListener
    {
        STRPM::ProxyMappingCallback Callback{ nullptr };
        void* UserData{ nullptr };
    };

    TiltedPhoques::Vector<MappingListener> m_mappingListeners;

    // Kept so Shutdown() can release them: the service is a singleton that
    // outlives the World whose registry they point at.
    entt::scoped_connection m_playerAddedConnection;
    entt::scoped_connection m_playerRemovedConnection;

    World* m_pWorld{ nullptr };
    std::uint64_t m_nextHandle{ 1 };
    TiltedPhoques::String m_localDisplayName;
};