#include <TiltedOnlinePCH.h>

#include <Services/DiscoveryService.h>
#include <Games/TES.h>

#include <Games/References.h>

#include <Forms/TESObjectCELL.h>
#include <Forms/TESForm.h>
#include <Forms/TESWorldSpace.h>
#include <Forms/TESNPC.h>

#include <Events/ActorAddedEvent.h>
#include <Events/ActorRemovedEvent.h>
#include <Events/PreUpdateEvent.h>
#include <Events/GridCellChangeEvent.h>
#include <Events/CellChangeEvent.h>
#include <Events/LocationChangeEvent.h>
#include <Events/ConnectedEvent.h>
#include <Events/ConnectionErrorEvent.h>

#include <World.h>

namespace
{
bool IsDefaultModlist(GameList<Mod>& aCurrentModlist) noexcept
{
    static const auto s_defaultModlist = std::to_array<TiltedPhoques::String>(
        {"Skyrim.esm", "Update.esm", "Dawnguard.esm", "HearthFires.esm", 
        "Dragonborn.esm", "_ResourcePack.esl", "SkyrimTogether.esp", "SkyrimTogetherQuestPatches.esp"}
    );

    if (aCurrentModlist.Size() != s_defaultModlist.size())
        return false;

    auto expectedMod = s_defaultModlist.begin();
    for (const auto* pCurrentMod : aCurrentModlist)
    {
        if (pCurrentMod->filename != *expectedMod)
            return false;

        ++expectedMod;
    }

    return true;
}
} // namespace

DiscoveryService::DiscoveryService(World& aWorld, entt::dispatcher& aDispatcher) noexcept
    : m_world(aWorld)
    , m_dispatcher(aDispatcher)
{
    m_preUpdateConnection = m_dispatcher.sink<PreUpdateEvent>().connect<&DiscoveryService::OnUpdate>(this);
    m_connectedConnection = m_dispatcher.sink<ConnectedEvent>().connect<&DiscoveryService::OnConnected>(this);

    EventDispatcherManager::Get()->loadGameEvent.RegisterSink(this);
}

void DiscoveryService::VisitCell(bool aForceTrigger) noexcept
{
    const PlayerCharacter* pPlayer = PlayerCharacter::Get();
    if (!pPlayer)
        return;

    if (pPlayer->GetWorldSpace())
        VisitExteriorCell(aForceTrigger);
    else if (pPlayer->GetParentCellEx())
        VisitInteriorCell(aForceTrigger);

    // exactly how the game does it too
    // Only compare/forward a location that is actually a live form: right after
    // a load the slot can still hold garbage, and the LocationChangeEvent
    // subscribers dereference it.
    TESForm* pLocation = pPlayer->locationForm;
    if (!IsPlausibleFormPointer(pLocation))
        pLocation = nullptr;

    if (m_pLocation != pLocation)
    {
        m_pLocation = pLocation;
        if (pLocation)
            m_dispatcher.trigger(LocationChangeEvent());
    }
}

void DiscoveryService::VisitExteriorCell(bool aForceTrigger) noexcept
{
    const PlayerCharacter* pPlayer = PlayerCharacter::Get();
    const auto pWorldSpace = pPlayer->GetWorldSpace();

    m_interiorCellId = 0;

    const TES* pTES = TES::Get();
    const uint32_t worldSpaceId = pWorldSpace->formID;
    const GridCellCoords gameCurrentGrid(pTES->currentGridX, pTES->currentGridY);
    const GridCellCoords gameCenterGrid(pTES->centerGridX, pTES->centerGridY);

    if (m_worldSpaceId != worldSpaceId || aForceTrigger)
    {
        DetectGridCellChange(pWorldSpace, true);
        // If the world space changes, then we want to send out a CellChangeEvent out too.
        aForceTrigger = true;
    }
    else if (gameCenterGrid != m_centerGrid)
    {
        DetectGridCellChange(pWorldSpace, false);
    }

    if (gameCurrentGrid != m_currentGrid || aForceTrigger)
    {
        CellChangeEvent cellChangeEvent{};

        if (!m_world.GetModSystem().GetServerModId(pWorldSpace->formID, cellChangeEvent.WorldSpaceId))
        {
            spdlog::error("Failed to find world space id for form id {:X}", pWorldSpace->formID);
            return;
        }

        TESObjectCELL* pCell = pPlayer->GetParentCellEx();
        if (!pCell)
            pCell = ModManager::Get()->GetCellFromCoordinates(gameCurrentGrid.X, gameCurrentGrid.Y, pWorldSpace, false);

        // The fallback walk can come back empty for a cell that is not attached
        // yet, which is exactly the state a load leaves behind.
        if (!pCell)
            return;

        if (!m_world.GetModSystem().GetServerModId(pCell->formID, cellChangeEvent.CellId))
        {
            spdlog::error("Failed to find cell id for form id {:X}", pCell->formID);
            return;
        }

        cellChangeEvent.CurrentCoords = gameCurrentGrid;

        m_dispatcher.trigger(cellChangeEvent);

        m_currentGrid = gameCurrentGrid;
    }
}

void DiscoveryService::VisitInteriorCell(bool aForceTrigger) noexcept
{
    ResetCachedCellData();

    // The player is between cells during a load, and the interior cell is then
    // null while the exterior walk above still succeeds. VisitCell() only checks
    // that this call is reached, not that the cell exists.
    const auto* pPlayer = PlayerCharacter::Get();
    const auto* pParentCell = pPlayer ? pPlayer->GetParentCellEx() : nullptr;
    if (!pParentCell)
        return;

    const uint32_t cellId = pParentCell->formID;
    if (m_interiorCellId != cellId || aForceTrigger)
    {
        CellChangeEvent cellChangeEvent{};

        if (!m_world.GetModSystem().GetServerModId(cellId, cellChangeEvent.CellId))
        {
            spdlog::error("Failed to find cell id {:X}", cellId);
            return;
        }

        m_dispatcher.trigger(cellChangeEvent);
        m_interiorCellId = cellId;
    }
}

void DiscoveryService::DetectGridCellChange(TESWorldSpace* aWorldSpace, bool aNewCellGrid) noexcept
{
    GridCellChangeEvent changeEvent{};

    const uint32_t worldSpaceId = aWorldSpace->formID;
    changeEvent.WorldSpaceId = worldSpaceId;

    const TES* pTES = TES::Get();
    const int32_t startGridX = pTES->centerGridX - GridCellCoords::m_gridsToLoad / 2;
    const int32_t startGridY = pTES->centerGridY - GridCellCoords::m_gridsToLoad / 2;

    for (int32_t i = 0; i < GridCellCoords::m_gridsToLoad; ++i)
    {
        for (int32_t j = 0; j < GridCellCoords::m_gridsToLoad; ++j)
        {
            // If it is a new cell grid, don't check for previously loaded cells.
            if (!aNewCellGrid)
            {
                if (GridCellCoords::IsCellInGridCell(m_centerGrid, {startGridX + i, startGridY + j}, false))
                    continue;
            }

            const TESObjectCELL* pCell = ModManager::Get()->GetCellFromCoordinates(startGridX + i, startGridY + j, aWorldSpace, 0);

            if (!pCell)
            {
                spdlog::warn("Cell not found at coordinates ({}, {}) in worldspace {:X}", startGridX + i, startGridY + j, aWorldSpace->formID);
                continue;
            }

            GameId cellId{};
            if (!m_world.GetModSystem().GetServerModId(pCell->formID, cellId))
            {
                spdlog::error("Failed to find cell id for form id {:X}", pCell->formID);
                continue;
            }

            changeEvent.Cells.push_back(cellId);
        }
    }

    // The player pointer itself is nullable early in a load, the parent cell is
    // nullable by definition, and the coordinate fallback is nullable too - the
    // same call is checked two other times in this file. Without this the formID
    // read below is the third null dereference on one line.
    auto* pPlayer = PlayerCharacter::Get();
    TESObjectCELL* pCell = pPlayer ? pPlayer->GetParentCellEx() : nullptr;
    if (!pCell)
        pCell = ModManager::Get()->GetCellFromCoordinates(pTES->currentGridX, pTES->currentGridY, aWorldSpace, false);

    if (!pCell)
    {
        spdlog::warn("Grid cell change for worldspace {:X} has no player cell to report", aWorldSpace->formID);
        return;
    }

    if (!m_world.GetModSystem().GetServerModId(pCell->formID, changeEvent.PlayerCell))
    {
        spdlog::error("Failed to find cell id for form id {:X}", pCell->formID);
        return;
    }

    changeEvent.CenterCoords = m_centerGrid = {pTES->centerGridX, pTES->centerGridY};

    m_dispatcher.trigger(changeEvent);

    m_worldSpaceId = worldSpaceId;
}

void DiscoveryService::VisitForms() noexcept
{
    static Set<uint32_t> s_previousForms;
    s_previousForms = m_forms;

    const auto visitor = [this](TESObjectREFR* apReference)
    {
        const auto formId = apReference->formID;

        if (!m_forms.count(formId))
        {
            m_forms.insert(formId);

            m_dispatcher.enqueue(ActorAddedEvent(formId));
        }
        else
            s_previousForms.erase(formId);
    };

    ProcessLists* const pProcessLists = ProcessLists::Get();
    if (!pProcessLists)
        return;

    for (uint32_t i = 0; i < pProcessLists->highActorHandleArray.length; ++i)
    {
        TESObjectREFR* const pRefr = TESObjectREFR::GetByHandle(pProcessLists->highActorHandleArray[i]);
        if (pRefr)
        {
            if (pRefr->GetNiNode())
            {
                visitor(pRefr);
            }
        }
    }

    // Not in actor holder. The visitor dereferences the reference immediately
    // (its formID), and the player does not exist at the main menu or before a
    // save is loaded, so this call must be guarded like VisitCell() above.
    if (auto* pPlayer = PlayerCharacter::Get())
        visitor(pPlayer);

    // We dispatch removal events first to prevent needless reallocations
    for (uint32_t formId : s_previousForms)
    {
        // A conform can remove both the 3D and the high-process handle. Keep the
        // existing discovery entry so rebuilding it does not cancel its assignment.
        // TODO: GetById performance in loop?
        if (auto* pActor = Cast<Actor>(TESForm::GetById(formId)); pActor && !pActor->IsDeleted())
        {
            using ReconciliationStage = ActorExtension::ReconciliationStage;
            const auto cStage = pActor->GetExtension()->Reconciliation;
            const auto* pCell = pActor->GetParentCellEx();
            // Finish the disable/enable pair even if the cell starts unloading.
            // Once enabled, an unloaded cell is a real removal.
            if (cStage == ReconciliationStage::WaitingForDisable || (cStage == ReconciliationStage::WaitingFor3D && pCell && pCell->IsAttached()))
            {
                continue;
            }
        }

        m_dispatcher.trigger(ActorRemovedEvent(formId));
        m_forms.erase(formId);
    }

    // Dispatch all adds
    m_dispatcher.update<ActorAddedEvent>();
}

void DiscoveryService::OnUpdate(const PreUpdateEvent& acUpdateEvent) noexcept
{
    TP_UNUSED(acUpdateEvent);

    VisitCell();
    VisitForms();
}

void DiscoveryService::OnConnected(const ConnectedEvent& acEvent) noexcept
{
    // uGridsToLoad should always be 5, as this is what the server enforces
    auto* pSetting = INISettingCollection::Get()->GetSetting("uGridsToLoad:General");
    if (pSetting && pSetting->data != 5)
    {
        ConnectionErrorEvent errorEvent{};
        errorEvent.ErrorDetail = "{\"error\": \"bad_uGridsToLoad\"}";

        m_world.GetRunner().Trigger(errorEvent);
    }

    VisitCell(true);
}

BSTEventResult DiscoveryService::OnEvent(const TESLoadGameEvent*, const EventDispatcher<TESLoadGameEvent>*)
{
    spdlog::info("Finished loading, triggering visit cell");

    if (!IsDefaultModlist(ModManager::Get()->mods))
    {
        ConnectionErrorEvent errorEvent{};
        errorEvent.ErrorDetail = "{\"error\": \"non_default_install\"}";

        m_world.GetRunner().Trigger(errorEvent);
    }

    VisitCell(true);

    return BSTEventResult::kOk;
}

void DiscoveryService::ResetCachedCellData() noexcept
{
    m_worldSpaceId = 0;
    m_centerGrid.Reset();
    m_currentGrid.Reset();
}
