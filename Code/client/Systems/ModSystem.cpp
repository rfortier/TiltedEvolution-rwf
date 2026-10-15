#include <TiltedOnlinePCH.h>

#include <Systems/ModSystem.h>

#include <Structs/Mods.h>
#include <Structs/GameId.h>

#include <Games/TES.h>

ModSystem::ModSystem(entt::dispatcher& aDispatcher) noexcept
{
    std::memset(m_standardToServer, 0, sizeof(m_standardToServer));
    // Deal with temporary ids
    m_standardToServer[0xFF] = std::numeric_limits<uint32_t>::max();

    m_modsConnection = aDispatcher.sink<Mods>().connect<&ModSystem::HandleMods>(this);
}

bool ModSystem::GetServerModId(const uint32_t aGameId, uint32_t& aModId, uint32_t& aBaseId) const noexcept
{
    uint32_t hiByte = aGameId >> 24;

    // If it's a lite mod
    if (hiByte == 0xFE)
    {
        const uint16_t liteId = ((aGameId & 0x00FFF000) >> 12) & 0xFFFF;

        const auto itor = m_liteToServer.find(liteId);
        if (itor != std::end(m_liteToServer))
        {
            aModId = itor->second;
            aBaseId = aGameId & 0x00000FFF;
            return true;
        }

        return false;
    }

    // Here we have a standard mod
    aModId = m_standardToServer[hiByte & 0xFF];
    aBaseId = aGameId & 0x00FFFFFFu;

    return true;
}

bool ModSystem::GetServerModId(uint32_t aGameId, GameId& aServerId) const noexcept
{
    return GetServerModId(aGameId, aServerId.ModId, aServerId.BaseId);
}

uint32_t ModSystem::GetGameId(uint32_t aServerId, uint32_t aFormId) const noexcept
{
    auto itor = m_serverToGame.find(aServerId);
    if (itor != std::end(m_serverToGame))
    {
        if (itor->second.isLite)
        {
            aFormId &= 0xFFFu;
            aFormId |= 0xFE000000u;
            aFormId |= uint32_t(itor->second.id) << 12;
        }
        else
        {
            aFormId &= 0x00FFFFFFu;
            aFormId |= uint32_t(itor->second.id) << 24;
        }

        return aFormId;
    }

    return 0;
}

uint32_t ModSystem::GetGameId(const GameId& acGameId) const noexcept
{
    return GetGameId(acGameId.ModId, acGameId.BaseId);
}

void ModSystem::HandleMods(const Mods& acMods) noexcept
{
    // The mod table the client builds from the server list. The maps and the reports
    // allocate; a table that could not be filled is filled again when the server
    // resends the list.
    try
    {
        m_serverToGame.clear();
        m_liteToServer.clear();
        std::memset(m_standardToServer, 0, sizeof(m_standardToServer));
        m_standardToServer[0xFF] = std::numeric_limits<uint32_t>::max();

        // The mod manager is built by the engine during startup, so it can still be
        // missing here. Every entry then falls into the "not found locally" branch
        // below, which is what an unresolvable mod is reported as.
        const auto pModManager = ModManager::Get();

        // Counted and reported for the same reason the reporting loop names what
        // it drops (Code/client/Services/Generic/TransportService.cpp): whether a
        // lite file survives IsLoaded() is not established in this repository, and
        // this pair of lines answers it from one player's log. Zero lite entries
        // beside a reported lite file is the defect; a non-zero count with a
        // non-zero mapping count is the path working end to end.
        uint32_t liteListed = 0;
        uint32_t unmapped = 0;

        for (const auto& mod : acMods.ModList)
        {
            if (mod.IsLite)
                ++liteListed;

            if (Mod* pMod = pModManager ? pModManager->GetByName(mod.Filename.c_str()) : nullptr)
            {
                if (mod.IsLite)
                {
                    m_serverToGame.emplace(mod.Id, GameMod{static_cast<uint16_t>(pMod->GetId() & 0xFFFu), true});
                    m_liteToServer.emplace(pMod->GetId(), mod.Id);
                }
                else
                {
                    m_serverToGame.emplace(mod.Id, GameMod{static_cast<uint16_t>(pMod->GetId() & 0xFFu), false});
                    m_standardToServer[pMod->GetId() & 0xFF] = mod.Id;
                }
            }
            else
            {
                ++unmapped;
                spdlog::error("Failed to find mod {}, is lite? {}, id: {:X}", mod.Filename.c_str(), mod.IsLite, mod.Id);
            }
        }

        spdlog::info("server mod list: {} entries, {} lite; lite mappings established: {}, not found locally: {}",
                     acMods.ModList.size(), liteListed, m_liteToServer.size(), unmapped);
    }
    catch (...)
    {
        Diagnostics::ReportFailure(__FUNCTION__, "ran out of memory in ModSystem::HandleMods");
    }
}
