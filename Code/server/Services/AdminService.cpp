#include <GameServer.h>

#include <World.h>
#include <Services/AdminService.h>

#include <AdminMessages/AdminShutdownRequest.h>

AdminService::AdminService(World& aWorld, entt::dispatcher& aDispatcher)
    : m_world(aWorld)
{
    m_shutdownConnection = aDispatcher.sink<AdminPacketEvent<AdminShutdownRequest>>().connect<&AdminService::HandleShutdown>(this);
}

void AdminService::HandleShutdown(const AdminPacketEvent<AdminShutdownRequest>& acMessage) noexcept
{
    // The report formats into a fresh buffer, which is the only thing here that can
    // allocate. The shutdown itself is what the admin asked for and it is issued either
    // way: a request that could not be logged is still a request.
    try
    {
        spdlog::warn("Shutdown was requested by {:x}", acMessage.ConnectionId);

        GameServer::Get()->Kill();
    }
    catch (...)
    {
        Diagnostics::ReportFailure(__FUNCTION__, "ran out of memory while logging a shutdown request");
    }
}

void AdminService::sink_it_(const spdlog::details::log_msg& msg)
{
    // This sink used to format every server log line into an admin-protocol ServerLogs and
    // hand it to the admin broadcast in GameServer.h, which addresses the admin *credential*
    // set. Every member of that set is an ordinary game connection: one is inserted while
    // HandleAuthenticationRequest is still running the game handshake
    // (Code/server/GameServer.cpp:925), the other writer resolves a player by username (:430),
    // and the admin client cannot get in at all - AdminApp::OnConnected sends an
    // AuthenticationRequest with an empty Version (Code/admin/Transport.cpp:45-50), and the
    // version check at :885 rejects it before the token is looked at at :918.
    //
    // The two protocols do not share a layout, only a numbering: kServerLogs and
    // kAssignCharacterResponse are both 1 (Code/admin_protocol/Opcodes.h:13,
    // Code/encoding/Opcodes.h:11), so a game client that received one of these frames would
    // parse a log line as a character assignment - Cookie, the actor value map, the
    // inventory, the action list - and believe whatever those bytes claimed.
    //
    // There is no connection this frame can be addressed to, so it is not built. The sink
    // stays registered (Code/server/World.cpp:24-25) so the stream is still visible to the
    // next reader; it was also the only reason a log line was formatted a second time on
    // every spdlog call. The stream can come back together with the server-side receive path
    // it needs: Code/server/GameServer.cpp:593-607 is where that dispatch would live.
    (void)msg;
}

void AdminService::flush_()
{
}
