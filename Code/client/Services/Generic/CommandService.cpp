
#include <Services/CommandService.h>
#include <Services/TransportService.h>

#include <Systems/ModSystem.h>
#include <World.h>

#include <Forms/TESObjectCELL.h>
#include <Forms/TESWorldSpace.h>
#include <PlayerCharacter.h>

#include <Events/SetTimeCommandEvent.h>

#include <Messages/TeleportCommandRequest.h>
#include <Messages/TeleportCommandResponse.h>
#include "Messages/SetTimeCommandRequest.h"
#include <Messages/NotifySetTimeResult.h>

#include <Structs/GridCellCoords.h>

CommandService::CommandService(World& aWorld, TransportService& aTransport, entt::dispatcher& aDispatcher)
    : m_world(aWorld)
    , m_transport(aTransport)
{
    m_setTimeConnection = aDispatcher.sink<SetTimeCommandEvent>().connect<&CommandService::OnSetTimeCommand>(this);
    m_teleportConnection = aDispatcher.sink<TeleportCommandResponse>().connect<&CommandService::OnTeleportCommandResponse>(this);
    m_setTimeResultConnection = aDispatcher.sink<NotifySetTimeResult>().connect<&CommandService::OnSetTimeResult>(this);
}

void CommandService::OnSetTimeCommand(const SetTimeCommandEvent& acEvent) const noexcept
{
    SetTimeCommandRequest request{};
    request.Hours = acEvent.Hours;
    request.Minutes = acEvent.Minutes;
    request.PlayerId = acEvent.PlayerId;
    m_transport.Send(request);
}

// The server always answers /settime, including when it refuses. Without this
// the refusal was indistinguishable from the command not existing at all: the
// server logged it, the player saw nothing, and the clock did not move.
void CommandService::OnSetTimeResult(const NotifySetTimeResult& acMessage) noexcept
{
    using RT = NotifySetTimeResult::SetTimeResult;

    if (acMessage.Result == RT::kSuccess)
        return; // the calendar update that follows is the visible result

    if (acMessage.Result == RT::kNoPermission)
        m_world.GetOverlayService().SendSystemMessage("Only an admin, or the party leader on a private server, can change the time.");
    else
        m_world.GetOverlayService().SendSystemMessage("That is not a time. Use /settime HH MM, with hours 0-23 and minutes 0-59.");
}

void CommandService::OnTeleportCommandResponse(const TeleportCommandResponse& acMessage) noexcept
{
    // The console command table and its reports. A command that could not be run
    // prints nothing; the console stays usable.
    try
    {
        spdlog::info("TeleportCommandResponse: {:X}:{:X} at position {}, {}, {}", acMessage.CellId.ModId, acMessage.CellId.BaseId, acMessage.Position.x, acMessage.Position.y, acMessage.Position.z);
        auto& modSystem = m_world.GetModSystem();

        const uint32_t cellId = modSystem.GetGameId(acMessage.CellId);
        TESObjectCELL* pCell = Cast<TESObjectCELL>(TESForm::GetById(cellId));
        if (!pCell)
        {
            const uint32_t worldSpaceId = modSystem.GetGameId(acMessage.WorldSpaceId);
            TESWorldSpace* pWorldSpace = Cast<TESWorldSpace>(TESForm::GetById(worldSpaceId));
            if (pWorldSpace)
            {
                GridCellCoords coordinates = GridCellCoords::CalculateGridCellCoords(acMessage.Position);
                pCell = pWorldSpace->LoadCell(coordinates.X, coordinates.Y);
            }

            if (!pCell)
            {
                spdlog::error("Failed to fetch cell to teleport to.");
                m_world.GetOverlayService().SendSystemMessage("Teleporting to player failed.");
                return;
            }
        }

        if (PlayerCharacter* pPlayer = PlayerCharacter::Get())
            pPlayer->MoveTo(pCell, acMessage.Position);
    }
    catch (...)
    {
        Diagnostics::ReportFailure(__FUNCTION__, "ran out of memory in CommandService::OnTeleportCommandResponse");
    }
}
