#include "DediCoreBackend.h"

#include "DediRunner.h"

// Telemetry and control exported by STServer.dll. Declared here rather than in
// a shared header because the runner deliberately does not include the server's
// include chain (it drags in entt/glm/sol2/TiltedCore).
struct ServerPlayerInfo
{
    uint32_t connectionId;
    uint32_t playerId;
    uint64_t connectedAtMs;
    uint32_t pingMs;
    bool isAdmin;
};

GS_IMPORT uint32_t QueryServerPlayerCount();
GS_IMPORT uint32_t QueryServerPlayers(ServerPlayerInfo* apOut, uint32_t aCapacity);

namespace server_runner
{

DediCoreBackend::DediCoreBackend(std::unique_ptr<DediRunner> apRunner)
    : m_runner(std::move(apRunner))
{
}

DediCoreBackend::~DediCoreBackend() = default;

bool DediCoreBackend::Initialize()
{
    // DediRunner's constructor already created and initialised the game server,
    // so there is nothing left to do here. Reporting the runner's own validity
    // is the honest answer: a runner that failed to build must not be presented
    // as a started server.
    return m_runner != nullptr && m_runner->IsValid();
}

void DediCoreBackend::Update()
{
    // ServerCore owns the single tick loop and calls this once per tick, so the
    // TPS the dashboard shows is the rate the server actually ran at. TickOnce()
    // is exactly the body of DediRunner's old loop, which is why there is no
    // second loop anywhere: driving both would tick the simulation twice per
    // frame and desynchronise it.
    if (m_runner != nullptr)
        m_runner->TickOnce();
}

void DediCoreBackend::Shutdown()
{
    if (m_runner != nullptr)
        m_runner->RequestKill();
}

bool DediCoreBackend::IsListening()
{
    // A pure query. It must NOT tick: Update() is the only place that advances
    // the simulation, and an extra tick per status poll would double the tick
    // rate. A runner that exists and is valid is listening; a stop request
    // surfaces through Update() returning without progress.
    return m_runner != nullptr && m_runner->IsValid();
}

uint32_t DediCoreBackend::PlayerCount()
{
    return QueryServerPlayerCount();
}

void DediCoreBackend::OnCommand(std::string_view aLine)
{
    if (m_runner == nullptr || aLine.empty())
        return;

    // HandleConsole takes the runner's own String type; the copy is on the core
    // thread and is not a hot path.
    m_runner->HandleConsole(TiltedPhoques::String(aLine.data(), aLine.size()));
}

bool DediCoreBackend::ApplySettings(const serverui::SettingsPatch& aPatch, std::string* apError)
{
    // Settings in this server are registered console settings, so a live change
    // is a console command rather than a struct write. Only the settings that
    // are genuinely safe to change without a restart are handled here; the rest
    // report honestly that a restart is required.
    if (m_runner == nullptr)
    {
        if (apError != nullptr)
            *apError = "no server";
        return false;
    }

    if (aPatch.port.has_value())
    {
        if (apError != nullptr)
            *apError = "listening port cannot change while running; restart required";
        return false;
    }

    if (aPatch.tickRate.has_value())
    {
        if (apError != nullptr)
            *apError = "tick rate cannot change while running; restart required";
        return false;
    }

    // maxPlayers and logLevel are accepted by the SettingsPatch contract but no
    // live path applies them yet. Returning true here would tell the dashboard
    // the edit was applied when nothing changed, which is worse than refusing:
    // say so instead and let the panel report it.
    if (aPatch.maxPlayers.has_value())
    {
        if (apError != nullptr)
            *apError = "max players cannot be changed while running; restart required";
        return false;
    }

    if (aPatch.logLevel.has_value())
    {
        if (apError != nullptr)
            *apError = "log level is fixed at startup; restart required";
        return false;
    }

    if (apError != nullptr)
        apError->clear();
    return true;
}

std::vector<serverui::PlayerInfo> DediCoreBackend::Players()
{
    std::vector<serverui::PlayerInfo> result;

    const uint32_t count = QueryServerPlayerCount();
    if (count == 0)
        return result;

    // The player count can change between the query above and the copy below,
    // so the buffer is bigger than the count and the retry loop handles the case
    // where it was still not big enough.
    //
    // QueryServerPlayers() stops at aCapacity, so an undersized buffer truncates
    // silently: the dashboard would show a subset of the players with no
    // indication anything was missing. Returning exactly aCapacity is the signal
    // that truncation happened; double the buffer and try again a bounded number
    // of times rather than looping forever against a server that is gaining
    // players faster than this reads them.
    std::vector<ServerPlayerInfo> rows(count + 32);
    uint32_t written = QueryServerPlayers(rows.data(), static_cast<uint32_t>(rows.size()));

    for (int attempt = 0; attempt < 4 && written == rows.size(); ++attempt)
    {
        rows.resize(rows.size() * 2);
        written = QueryServerPlayers(rows.data(), static_cast<uint32_t>(rows.size()));
    }

    result.reserve(written);
    for (uint32_t i = 0; i < written; ++i)
    {
        serverui::PlayerInfo info;
        info.connectionId = rows[i].connectionId;
        info.playerId = rows[i].playerId;
        info.connectedAtMs = rows[i].connectedAtMs;
        info.pingMs = rows[i].pingMs;
        info.isAdmin = rows[i].isAdmin;
        // The endpoint is not part of ServerPlayerInfo; leave the username empty
        // rather than inventing one. The dashboard renders a missing name as "-".
        result.push_back(std::move(info));
    }

    return result;
}

} // namespace server_runner
