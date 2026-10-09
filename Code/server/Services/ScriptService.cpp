#include <stdafx.h>

#include <Services/CalendarService.h>
#include <Services/ScriptService.h>
#include <World.h>

#include <Events/PlayerEnterWorldEvent.h>
#include <Events/UpdateEvent.h>

#include <Components.h>
#include <GameServer.h>
#include <TiltedCore/Filesystem.hpp>

#include <resources/ResourceCollection.h>

namespace Script
{
void CreateScriptBindings(sol::state& aState);
}

namespace
{
int ScriptExceptionHandler(lua_State* L, sol::optional<const std::exception&> maybe_exception,
                         sol::string_view description)
{
    // L is the lua state, which you can wrap in a state_view if necessary
    // maybe_exception will contain exception, if it exists
    // description will either be the what() of the exception or a description saying that we hit the general-case
    // catch(...)

    // ...

    fmt::print("An exception occurred in a function, here's what it says ");
    if (maybe_exception)
    {
        fmt::print("(straight from the exception): ");
        const std::exception& ex = *maybe_exception;
        fmt::print("{}\n", ex.what());
    }
    else
    {
        fmt::print("(from the description parameter): ");
        fmt::print("{}", std::string_view(description.data(), description.size()));
        fmt::print("\n");
    }


    // you must push 1 element onto the stack to be
    // transported through as the error object in Lua
    // note that Lua -- and 99.5% of all Lua users and libraries -- expects a string
    // so we push a single string (in our case, the description of the error)
    return sol::stack::push(L, description);
}
}

ScriptService::ScriptService(World& aWorld, entt::dispatcher& aDispatcher)
    : m_world(aWorld), m_updateConnection(aDispatcher.sink<UpdateEvent>().connect<&ScriptService::OnUpdate>(this)),
      m_playerEnterWorldConnection(
          aDispatcher.sink<PlayerEnterWorldEvent>().connect<&ScriptService::OnPlayerEnterWorld>(this))
{
}

ScriptService::~ScriptService()
{
    // A destructor is implicitly noexcept, and both of these are Lua calls that
    // allocate. An exception escaping here during unwinding is std::terminate with
    // no dump, which is a worse outcome than leaking a state that is being torn
    // down anyway.
    try
    {
        auto &lockedState = m_lua.Lock().Get();
        for (auto& m : m_sandboxes)
        {
            m.clear();
        }

        lockedState.collect_garbage();
    }
    catch (...)
    {
        ReportFailure(__FUNCTION__, "ran out of memory while tearing down the Lua state");
    }
}

void ScriptService::Initialize(Resources::ResourceCollection& aCollection) noexcept
{
    // Every line of this is allocation: a Lua library, the globals table, a
    // registry slot per bound function, the package path, and a path built per
    // manifest. It is called from GameServer::Initialize, before the server
    // listens, so there is no caller that could report the failure - the promise
    // is ours to keep. The server comes up without scripts rather than not coming
    // up at all, and says so once.
    try
    {
        auto lua = m_lua.Lock();
        auto& luaVm = lua.Get();
        luaVm.set_exception_handler(ScriptExceptionHandler);

        m_globals = luaVm.globals();

        luaVm.open_libraries(sol::lib::base, sol::lib::string, sol::lib::io, sol::lib::math, sol::lib::package,
                             sol::lib::os, sol::lib::table, sol::lib::bit32);

        // make sure to set package path to current directory scope
        // as this could get overriden by LUA_PATH environment variable
        luaVm["package"]["path"] = "./?.lua";

        BindInbuiltFunctions();

        aCollection.ForEachManifest([&](const Resources::Manifest001& aManifest) {
            if (aManifest.EntryPoint.empty()) // just a dependency?
                return;
            auto entryPointPath = aCollection.GetResourceFolderPath() / aManifest.FolderName / aManifest.EntryPoint;
            if (!std::filesystem::exists(entryPointPath))
            {
                spdlog::warn("Script entry point {} does not exist", entryPointPath.string());
                return;
            }
            LoadScript(entryPointPath);
        });
    }
    catch (...)
    {
        ReportFailure(__FUNCTION__, "ran out of memory while setting up the Lua state; no script will run");
    }
}

bool ScriptService::LoadScript(const std::filesystem::path& aPath)
{
    try
    {
        auto lua = m_lua.Lock();
        auto& luaVm = lua.Get();

        auto &env = m_sandboxes.emplace_back(luaVm, sol::create, luaVm.globals());
        
        const sol::protected_function_result result = luaVm.safe_script_file(aPath.string(), env);
        if (!result.valid())
        {
            const sol::error error = result;
            spdlog::error("Failed to load script file: {}", error.what());
            return false;
        }
    }
    catch (std::exception& exception)
    {
        spdlog::error("Failed to load script file: {}", exception.what());
        return false;
    }

    return true;
}

void ScriptService::BindInbuiltFunctions()
{
    auto lua = m_lua.Lock();
    auto& luaVm = lua.Get();
    Script::CreateScriptBindings(luaVm);

    {
        luaVm.set_function("addEventHandler", [this](std::string acName, sol::function aFunction) {
            AddEventHandler(std::move(acName), std::move(aFunction));
        });
        luaVm.set_function("cancelEvent", [this](std::string acReason) { CancelEvent(std::move(acReason)); });
    }
}

std::tuple<bool, String> ScriptService::HandleCharacterMove(const entt::entity aNpc) noexcept
{
    return CallCancelableEvent("onCharacterMove", aNpc);
}

std::tuple<bool, String> ScriptService::HandleCharacterSpawn(const entt::entity aNpc) noexcept
{
    return CallCancelableEvent("onCharacterSpawn", aNpc);
}

std::tuple<bool, String> ScriptService::HandleCharacterDestoy(const entt::entity aNpc) noexcept
{
    return CallCancelableEvent("onCharacterDestroy", aNpc);
}

std::tuple<bool, String> ScriptService::HandlePlayerJoin(const ConnectionId_t aPlayer) noexcept
{
    return CallCancelableEvent("onPlayerJoin", aPlayer);
}

std::tuple<bool, String> ScriptService::HandleChatMessage(const entt::entity aSender, const String& aMessage) noexcept
{
    return CallCancelableEvent("onChatMessage", aSender, aMessage);
}

void ScriptService::HandlePlayerQuit(ConnectionId_t aConnectionId, Server::EDisconnectReason aReason) noexcept
{
    // A pointer, not a std::string: the reason is one of six literals, and building
    // the string was the last allocation left on the way into CallEvent - on the
    // outside of its try block, in a noexcept function, on the disconnect path
    // where the server is already shedding state.
    const char* reason = "Unknown";

    switch (aReason)
    {
    case Server::EDisconnectReason::Quit:
        reason = "Quit";
        break;
    case Server::EDisconnectReason::Kicked:
        reason = "Kicked";
        break;
    case Server::EDisconnectReason::Banned:
        reason = "Banned";
        break;
    case Server::EDisconnectReason::BadConnection:
        reason = "Connection lost";
        break;
    case Server::EDisconnectReason::TimedOut:
        reason = "Timed out";
        break;
    case Server::EDisconnectReason::Unknown:
    default:
        break;
    }

    CallEvent("onPlayerQuit", aConnectionId, reason);
}

std::tuple<bool, String> ScriptService::HandleSetTime(int aHours, int aMinutes, float aTimeScale) noexcept
{
    return CallCancelableEvent("onSetTime", aHours, aMinutes, aTimeScale);
}

void ScriptService::OnUpdate(const UpdateEvent& acEvent) noexcept
{
    if (m_sandboxes.size() == 0)
        return;

    // This used to be wrapped in a try/catch for sol::error, which was the only
    // thing that could come out of the call at the time. The handler now lives in
    // CallEvent itself - a Lua callback is not the only way that call can fail, so
    // the boundary had to move down to where the allocations are. Keeping a second
    // catch here would also mean keeping an allocating log call inside a noexcept
    // tick handler, which is the very thing being fixed.
    CallEvent("onUpdate", acEvent.Delta);
}

void ScriptService::OnPlayerEnterWorld(const PlayerEnterWorldEvent& acEvent) noexcept
{
    /* const Script::Player cPlayer(acEvent.Entity, m_world);

    CallEvent("onPlayerEnterWorld", cPlayer);*/
}

void ScriptService::ReportFailure(const char* acpContext, const char* acpDetail) noexcept
{
    try
    {
        spdlog::error("{}: {}", acpContext, acpDetail);
    }
    catch (...)
    {
        // Nothing left to say it with. The alternative - letting the report of a
        // failure become the failure - is strictly worse.
    }
}

void ScriptService::AddEventHandler(const std::string acName, const sol::function acFunction) noexcept
{
    // Called from the 'addEventHandler' binding, which Lua reaches while a script
    // is loading. A registration that could not be stored is simply not there: the
    // event it asked for will find no handler, which is the same outcome as the
    // script never having asked. Letting it through instead would terminate the
    // server from inside a Lua call.
    try
    {
        m_callbacks[String(acName)].push_back(acFunction);
    }
    catch (...)
    {
        ReportFailure(__FUNCTION__, "ran out of memory while registering an event handler");
    }
}

void ScriptService::CancelEvent(const std::string aReason) noexcept
{
    // The flag is set before the reason is copied, so a failure here still cancels
    // the event and only loses its text. That order is deliberate: cancellation is
    // what the caller asked for, the reason is only how it is reported.
    m_eventCanceled = true;

    try
    {
        m_cancelReason = aReason;
    }
    catch (...)
    {
        ReportFailure(__FUNCTION__, "ran out of memory while storing the cancellation reason");
    }
}
