
#include "GameServer.h"

#include <common/GameServerInstance.h>

#ifdef _WIN32
#define GS_EXPORT __declspec(dllexport)
#else
#define GS_EXPORT __attribute__((visibility("default")))
#endif

namespace
{
constexpr char kBuildTag[]{BUILD_BRANCH "@" BUILD_COMMIT};
} // namespace

struct GameServerInstance final : IGameServerInstance
{
    GameServerInstance(Console::ConsoleRegistry& aConsole)
        : m_gameServer(aConsole)
    {
    }

    // to make sure our dtor is called.
    ~GameServerInstance() override = default;

    // Inherited via IGameServerInstance
    bool Initialize() override;
    void Shutdown() override;
    bool IsListening() override;
    bool IsRunning() override;
    void Update() override;

private:
    GameServer m_gameServer;
};

bool GameServerInstance::Initialize()
{
    m_gameServer.Initialize();
    return true;
}

void GameServerInstance::Shutdown()
{
    m_gameServer.Kill();
}

bool GameServerInstance::IsListening()
{
    return m_gameServer.IsListening();
}

bool GameServerInstance::IsRunning()
{
    return m_gameServer.IsRunning();
}

void GameServerInstance::Update()
{
    m_gameServer.Update();
}

// NOTE(Vince): For now we use this to compare the dll to the server.
GS_EXPORT const char* GetBuildTag()
{
    return kBuildTag;
}

GS_EXPORT bool CheckBuildTag(const char* apBuildTag)
{
    return std::strcmp(apBuildTag, kBuildTag) == 0;
}

// telemetry for the runner's control panel; the gui must not reach into
// GameServer.h directly (it drags the whole server include chain along)
GS_EXPORT uint32_t QueryServerPlayerCount()
{
    auto* pServer = GameServer::Get();
    if (pServer == nullptr)
        return 0;

    return static_cast<uint32_t>(pServer->GetWorld().GetPlayerManager().Count());
}

GS_EXPORT void RequestServerShutdown()
{
    if (GameServer::Get() != nullptr)
        GameServer::Get()->Kill();
}

// Flat, POD mirror of the player fields the control panel needs. Kept free of
// STL containers so the struct layout crossing the DLL boundary is unambiguous.
struct ServerPlayerInfo
{
    uint32_t connectionId;
    uint32_t playerId;
    uint64_t connectedAtMs;
    uint32_t pingMs;
    bool isAdmin;
};

// Copies up to aCapacity rows into apOut and returns how many were written.
// The caller (the runner's UI thread) owns the buffer; nothing internal is
// exposed, and the endpoint string is deliberately NOT copied here because it
// is a TiltedPhoques::String whose lifetime the caller does not control. The
// runner gets endpoints through a separate, equally copy-based call.
//
// Threading: this walks the player manager from the caller's thread. The
// existing QueryServerPlayerCount() already does the same, so this adds no new
// race that did not exist before - but it is exactly the unlocked cross-thread
// read documented in docs/design/SERVER-UI-ARCHITECTURE.md (finding F10) and
// should be moved onto the server thread when that is addressed.
GS_EXPORT uint32_t QueryServerPlayers(ServerPlayerInfo* apOut, uint32_t aCapacity)
{
    auto* pServer = GameServer::Get();
    if (pServer == nullptr || apOut == nullptr)
        return 0;

    const auto& adminSessions = pServer->GetAdminSessions();

    uint32_t written = 0;
    for (Player* pPlayer : pServer->GetWorld().GetPlayerManager())
    {
        if (written >= aCapacity)
            break;

        ServerPlayerInfo& row = apOut[written++];
        row.connectionId = pPlayer->GetConnectionId();
        row.playerId = pPlayer->GetId();
        row.connectedAtMs = pPlayer->GetConnectedAtMs();
        row.pingMs = pPlayer->GetPingMs();
        // find() rather than contains(): TiltedPhoques::Set is a tiltedcore
        // type whose exact API surface is not readable in this repo, and the one
        // .contains() call site in GameServer.cpp is commented out. find() works
        // for any std::set-compatible container.
        row.isAdmin = adminSessions.find(pPlayer->GetConnectionId()) != adminSessions.end();
    }

    return written;
}

// Registers the server's static settings, must be called before loading any settings
GS_EXPORT void RegisterServerSettings(Console::ConsoleRegistry& aConReg)
{
    aConReg.BindStaticItems();
}

GS_EXPORT UniquePtr<IGameServerInstance> CreateGameServer(Console::ConsoleRegistry& aConReg)
{
    return TiltedPhoques::CastUnique<IGameServerInstance>(TiltedPhoques::MakeUnique<GameServerInstance>(aConReg));
}

// cxx symbol

// These two are windows specific implementation details, since all symbols & insances are private there, so we must hand over
// the control.
// if not compiling with -fvisibility=hidden, hide these

GS_EXPORT void SetDefaultLogger(std::shared_ptr<spdlog::logger> aLogger)
{
    // #ifdef _WIN32
    spdlog::set_default_logger(std::move(aLogger));
    // #endif
}

GS_EXPORT void RegisterLogger(std::shared_ptr<spdlog::logger> aLogger)
{
    // #ifdef _WIN32
    //  yes this needs to be here, else the dedirunner dies
    spdlog::register_logger(std::move(aLogger));
    // #endif
}

#ifdef _WIN32
// Before you think about moving logic in here...
// There are significant limits on what you can safely do in a DLL entry point. See General Best Practices for specific
// Windows APIs that are unsafe to call in DllMain. If you need anything but the simplest initialization then do that in
// an initialization function for the DLL. You can require applications to call the initialization function after
// DllMain has run and before they call any other functions in the DLL.
BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpReserved)
{
    switch (fdwReason)
    {
    case DLL_PROCESS_ATTACH: break;
    case DLL_PROCESS_DETACH: break;
    }
    return TRUE;
}
#endif
