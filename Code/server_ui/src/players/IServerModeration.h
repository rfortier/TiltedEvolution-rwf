#pragma once

#include "../core/ServerCore.h"

#include <cstdint>
#include <string>
#include <vector>

namespace serverui
{

// The seam the real dedicated server implements once it is wired up.
//
// Everything under src/players/ is pure logic: it decides WHAT should happen and
// never calls the server. This interface is the only place where a decision
// becomes an action, so the whole moderation path stays testable with a fake
// adapter and no game server at all.
//
// ---------------------------------------------------------------------------
// Integration map - what each method will have to call in Code/server/.
// ---------------------------------------------------------------------------
//
// CloseConnection(aConnectionId) - kick and ban enforcement.
//   STATUS: UNKNOWN, and it is the one blocking unknown.
//   There is no kick/ban/close call anywhere in Code/server/, Code/common/ or
//   Code/encoding/ (verified by grep for kick|ban: zero hits). The close path
//   lives in TiltedPhoques::Server, which is the tiltedcore xmake package and is
//   NOT vendored under Libraries/, so its signature cannot be read in this
//   repository. Whoever integrates this must first identify the connection-close
//   entry point (candidates to check: the Server base class's session/close API,
//   or GameServer's own transport wrapper) and then implement this method on top
//   of it. Until then it must return false, not pretend to have kicked.
//
// SendDirectMessage(aConnectionId, acText) - direct message.
//   STATUS: KNOWN, no new wire protocol needed.
//   Build a NotifyChatMessageBroadcast (Code/encoding/Messages/
//   NotifyChatMessageBroadcast.h: fields MessageType, PlayerName, ChatMessage)
//   and deliver it to exactly one connection:
//       GameServer::Get()->Send(aConnectionId, message);   // GameServer.h:57
//   The message must be serialised with ChatSanitize applied the same way
//   OverlayService::HandleChatMessage does for ordinary chat, so a DM cannot
//   carry control characters a normal chat could not.
//   Player::Send(const ServerMessage&) (Player.h:43) is the per-player variant
//   when a Player* is already in hand; both reach the same connection.
//
// CurrentPlayers() - the dashboard's player table.
//   STATUS: PARTIALLY KNOWN.
//   Connection id, player id, username and endpoint are available today:
//       Player::GetConnectionId()  Player.h:16
//       Player::GetId()            Player.h:15
//       Player::GetUsername()      Player.h:20
//       Player::GetEndPoint()      Player.h:21
//   Admin flag:
//       GameServer::Get()->GetAdminSessions()   GameServer.h:90
//       (or ForEachAdmin at GameServer.h:73)
//   STILL MISSING on the server side, and this is a server change, not a
//   server_ui change:
//       * connectedAtMs - Player has no join timestamp. Add one to Player (or
//         record it in the World when the player entity is created).
//       * pingMs - Player has no RTT field. Until a real RTT measurement is
//         wired up, return 0 (= unknown, PlayerRegistry::kPingUnknown). Do not
//         invent a value.
//   Until those two exist, the adapter fills connectedAtMs from its own join
//   observation and pingMs with 0.
//
// Threading: every method is called on the core thread. CurrentPlayers() hands
// back value-typed PlayerRow records only, so nothing server owned escapes.
struct IServerModeration
{
    virtual ~IServerModeration() = default;

    // Kick / ban enforcement. false when the connection is already gone or the
    // close could not be performed - the caller must not report success then.
    virtual bool CloseConnection(uint32_t aConnectionId) = 0;

    // NotifyChatMessageBroadcast to one connection. false on failure.
    virtual bool SendDirectMessage(uint32_t aConnectionId, const std::string& acText) = 0;

    // The live player list. PlayerRow::username points at storage the adapter
    // owns; it must stay valid until the next call.
    virtual std::vector<PlayerRow> CurrentPlayers() = 0;
};

} // namespace serverui
