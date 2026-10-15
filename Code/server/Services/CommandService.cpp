#include <Services/CommandService.h>

#include <Components.h>
#include <GameServer.h>
#include <World.h>

#include <Messages/SetTimeCommandRequest.h>
#include <Messages/NotifySetTimeResult.h>
#include <Messages/TeleportCommandRequest.h>
#include <Messages/TeleportCommandResponse.h>

#include <Setting.h>

// This file used to declare its own
//   Console::Setting bAnnounceServer{"LiveServices:bAnnounceServer", ...};
// while GameServer.cpp declared the same name for the same question. Settings
// self-register into one global list in static-initialisation order - unspecified
// across translation units - and ConsoleRegistry::FindSetting takes the FIRST
// match by name, so exactly one of the two objects ever received the ini value and
// the other kept its default forever. Which one won was not even stable between
// builds. There is one setting now, and it lives with the other server settings in
// GameServer.cpp, where IsPublicServer() is the accessor both readers use.

CommandService::CommandService(World& aWorld, entt::dispatcher& aDispatcher) noexcept
    : m_world(aWorld)
{
    m_setTimeConnection = aDispatcher.sink<PacketEvent<SetTimeCommandRequest>>().connect<&CommandService::OnSetTimeCommand>(this);
    m_teleportConnection = aDispatcher.sink<PacketEvent<TeleportCommandRequest>>().connect<&CommandService::OnTeleportCommandRequest>(this);
}

void CommandService::OnSetTimeCommand(const PacketEvent<SetTimeCommandRequest>& acMessage) const noexcept
{
    NotifySetTimeResult response{};

    // Admin override: always allowed.
    //
    // The decision is made from the connection this packet arrived on, never from
    // Packet.PlayerId. That field is filled in by the sender itself - the client
    // writes its own id into it at OverlayClient.cpp:163 - and every player learns
    // every other player's id from NotifyPlayerJoined, so the old test ("some
    // admin's id equals the id I claim") let any player pass as an admin by naming
    // one. The set holds connection ids, and the sender's own connection id is the
    // only thing in this handler that cannot be chosen by the sender.
    //
    // Reading the set this way also removes the null dereference the old loop had.
    // GetByConnectionId answers null for a session that has no player row, and an
    // admin session is not guaranteed to have one: the connection id is inserted at
    // GameServer.cpp:909, the row is only created at :990, and the authentication
    // path has three ways to leave without the pair matching - a mod-policy mismatch
    // returns at :960 (no row was ever made), a duplicate authentication returns at
    // :996, and a script that cancels the join removes the row again at :1013. None
    // of them goes back to clean up the set, which is only ever erased on disconnect
    // (GameServer.cpp:612).
    if (GameServer::Get()->GetAdminSessions().contains(acMessage.pPlayer->GetConnectionId()))
    {
        const auto cHours = static_cast<int>(acMessage.Packet.Hours);
        const auto cMinutes = static_cast<int>(acMessage.Packet.Minutes);

        // SetTime refuses hours, minutes and a scale it cannot apply, and the reply
        // has to say which of the two happened: answering success for a call that
        // changed nothing left the player waiting for a calendar resync that was
        // never coming.
        if (m_world.GetCalendarService().SetTime(cHours, cMinutes, m_world.GetCalendarService().GetTimeScale()))
            response.Result = NotifySetTimeResult::SetTimeResult::kSuccess;
        else
            response.Result = NotifySetTimeResult::SetTimeResult::kInvalidTime;

        acMessage.pPlayer->Send(response);

        return;
    }

    // Party leader allowed on private servers only. Asked through the one setting
    // rather than a per-file copy: see the note at the top of this file.
    const auto* pPartyService = &m_world.GetPartyService();
    if (pPartyService->IsPlayerLeader(acMessage.pPlayer) && !GameServer::Get()->IsPublicServer())
    {
        const auto cHours = static_cast<int>(acMessage.Packet.Hours);
        const auto cMinutes = static_cast<int>(acMessage.Packet.Minutes);

        if (m_world.GetCalendarService().SetTime(cHours, cMinutes, m_world.GetCalendarService().GetTimeScale()))
            response.Result = NotifySetTimeResult::SetTimeResult::kSuccess;
        else
            response.Result = NotifySetTimeResult::SetTimeResult::kInvalidTime;

        acMessage.pPlayer->Send(response);

        return;
    }

    response.Result = NotifySetTimeResult::SetTimeResult::kNoPermission;
    acMessage.pPlayer->Send(response);
}

void CommandService::OnTeleportCommandRequest(const PacketEvent<TeleportCommandRequest>& acMessage) const noexcept
{
    Player* pTargetPlayer = nullptr;
    for (Player* pPlayer : m_world.GetPlayerManager())
    {
        if (pPlayer->GetUsername() == acMessage.Packet.TargetPlayer)
            pTargetPlayer = pPlayer;
    }

    TeleportCommandResponse response{};
    if (pTargetPlayer)
    {
        auto character = pTargetPlayer->GetCharacter();
        if (character)
        {
            const auto* pMovementComponent = m_world.try_get<MovementComponent>(*character);
            if (pMovementComponent)
            {
                const auto& cellComponent = pTargetPlayer->GetCellComponent();
                response.CellId = cellComponent.Cell;
                response.Position = pMovementComponent->Position;
                response.WorldSpaceId = cellComponent.WorldSpaceId;
            }
        }
    }

    acMessage.pPlayer->Send(response);
}
