#pragma once

#include <Events/PacketEvent.h>
#include <Events/UpdateEvent.h>
#include <TiltedCore/Lockable.hpp>

struct World;
struct ClientRpcCalls;
struct PlayerEnterWorldEvent;

namespace Resources
{
struct ResourceCollection;
}

struct ScriptService
{
    ScriptService(World& aWorld, entt::dispatcher& aDispatcher);
    ~ScriptService();

    TP_NOCOPYMOVE(ScriptService);

    void Initialize(Resources::ResourceCollection& aCollection) noexcept;
    bool LoadScript(const std::filesystem::path& aPath);

    std::tuple<bool, String> HandlePlayerJoin(const ConnectionId_t aEntity) noexcept;

    std::tuple<bool, String> HandleCharacterMove(const entt::entity aNpc) noexcept;
    std::tuple<bool, String> HandleCharacterSpawn(const entt::entity aPlayer) noexcept;
    std::tuple<bool, String> HandleCharacterDestoy(const entt::entity aPlayer) noexcept;

    std::tuple<bool, String> HandleChatMessage(const entt::entity aSender, const String& aMessage) noexcept;

    void HandlePlayerQuit(ConnectionId_t aConnectionId, Server::EDisconnectReason aReason) noexcept;

    std::tuple<bool, String> HandleSetTime(int aHours, int aMinutes, float aTimeScale) noexcept;

  protected:
    // void RegisterExtensions(ScriptContext& aContext) override;

    void OnUpdate(const UpdateEvent& acEvent) noexcept;
    void OnPlayerEnterWorld(const PlayerEnterWorldEvent& acEvent) noexcept;
    // void BindStaticFunctions(ScriptContext& aContext) noexcept;
    // void BindTypes(ScriptContext& aContext) noexcept;

    void AddEventHandler(std::string acName, sol::function acFunction) noexcept;
    void CancelEvent(std::string aReason) noexcept;

    // The name is a literal at every call site below, and it used to be a
    // `const String&`: that made each of the six noexcept entry points build a
    // temporary String *outside* the callee's try block, on the way in, where
    // nothing can catch it. Taking the pointer keeps the only allocation inside
    // the function that now handles it.
    template <typename... Args>
    std::tuple<bool, String> CallCancelableEvent(const char* acName, Args&&... args) noexcept;

    template <typename... Args> void CallEvent(const char* acName, Args&&... args) noexcept;

  private:
    void BindInbuiltFunctions();

    // Never throws, and that is the whole point of it: the callers below are
    // noexcept and use it as their handler, so it must not allocate. spdlog::error
    // formats its arguments into a fresh buffer, which under an exhausted heap
    // throws a second time - out of the one function that promised not to let
    // anything through. Both arguments must outlive the call and neither may be
    // built by it.
    static void ReportFailure(const char* acpContext, const char* acpDetail) noexcept;

  private:
    using TCallbacks = Vector<sol::function>;

    World& m_world;
    bool m_eventCanceled{};
    String m_cancelReason;

    entt::scoped_connection m_updateConnection;
    entt::scoped_connection m_rpcCallsRequest;
    entt::scoped_connection m_playerEnterWorldConnection;

    // NOTE(Vince): keep in mind that cxx specifies construction and deconstruction order,
    // so do not touch this order of member variables
    TiltedPhoques::Lockable<sol::state, std::recursive_mutex> m_lua;
    Map<String, TCallbacks> m_callbacks;
    TiltedPhoques::Vector<sol::environment> m_sandboxes;
    sol::table m_globals{};
};

#include "ScriptService.inl"
