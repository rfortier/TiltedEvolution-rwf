#include <Services/CommandService.h>

#include <Components.h>
#include <GameServer.h>
#include <World.h>

#include <Messages/SetTimeCommandRequest.h>
#include <Messages/NotifySetTimeResult.h>
#include <Messages/TeleportCommandRequest.h>
#include <Messages/TeleportCommandResponse.h>

#include <Setting.h>

namespace
{
Console::Setting bAnnounceServer{"LiveServices:bAnnounceServer", "Whether to list the server on the public server list", false};
}

CommandService::CommandService(World& aWorld, entt::dispatcher& aDispatcher) noexcept
    : m_world(aWorld)
{
    m_setTimeConnection = aDispatcher.sink<PacketEvent<SetTimeCommandRequest>>().connect<&CommandService::OnSetTimeCommand>(this);
    m_teleportConnection = aDispatcher.sink<PacketEvent<TeleportCommandRequest>>().connect<&CommandService::OnTeleportCommandRequest>(this);
}

void CommandService::OnSetTimeCommand(const PacketEvent<SetTimeCommandRequest>& acMessage) const noexcept
{
    NotifySetTimeResult response{};

    const auto cPlayerId = static_cast<uint32_t>(acMessage.Packet.PlayerId);

    // Admin override: always allowed.
    //
    // GetByConnectionId answers null for a session that has no player row, and
    // an admin session is not guaranteed to have one. The connection id is
    // inserted at GameServer.cpp:896, the row is only created at :977, and the
    // authentication path has three ways to leave without the pair matching: a
    // mod-policy mismatch returns at :947 (no row was ever made), a duplicate
    // authentication returns at :983, and a script that cancels the join removes
    // the row again at :999. None of them goes back to clean up the set, which
    // is only ever erased on disconnect (GameServer.cpp:612).
    //
    // The rest of this codebase already treats that as reachable rather than
    // theoretical - the sibling readers of the same set all guard, and one of
    // them logs it by name ("Admin session not found", GameServer.cpp:496-502);
    // GetAdminByUsername guards too (:1096, :1110). This loop was the only
    // reader that did not, and dereferencing the null here killed the whole
    // server from any player's /settime chat command, not just an admin's.
    for (const auto session : GameServer::Get()->GetAdminSessions())
    {
        const auto* pAdmin = PlayerManager::Get()->GetByConnectionId(session);
        if (!pAdmin)
            continue;

        if (pAdmin->GetId() == cPlayerId)
        {
            const auto cHours = static_cast<int>(acMessage.Packet.Hours);
            const auto cMinutes = static_cast<int>(acMessage.Packet.Minutes);

            m_world.GetCalendarService().SetTime(cHours, cMinutes, m_world.GetCalendarService().GetTimeScale());

            response.Result = NotifySetTimeResult::SetTimeResult::kSuccess;
            acMessage.pPlayer->Send(response);

            return;
        }
    }

    // Party leader allowed on private servers only
    const auto* pPartyService = &m_world.GetPartyService();
    if (pPartyService->IsPlayerLeader(acMessage.pPlayer) && !bAnnounceServer)
    {
        const auto cHours = static_cast<int>(acMessage.Packet.Hours);
        const auto cMinutes = static_cast<int>(acMessage.Packet.Minutes);

        m_world.GetCalendarService().SetTime(cHours, cMinutes, m_world.GetCalendarService().GetTimeScale());

        response.Result = NotifySetTimeResult::SetTimeResult::kSuccess;
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
