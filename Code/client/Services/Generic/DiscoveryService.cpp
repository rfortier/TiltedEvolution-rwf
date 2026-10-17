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

    // The event dispatcher is built by the engine during startup, and this
    // service is constructed from World's own initialisation, so the order is
    // not guaranteed. Registering no sink means the cell visit simply never
    // fires; dereferencing a null dispatcher takes the process down instead.
    if (auto* pEventList = EventDispatcherManager::Get())
        pEventList->loadGameEvent.RegisterSink(this);
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
    // The LAN discovery responder, whose reports and peer list allocate. A reply that
    // could not be built is lost; the next broadcast goes out a second later.
    try
    {
        const PlayerCharacter* pPlayer = PlayerCharacter::Get();
        const auto pWorldSpace = pPlayer ? pPlayer->GetWorldSpace() : nullptr;

        m_interiorCellId = 0;

        const TES* pTES = TES::Get();
        // Both singletons are null until the engine builds them, and the reply cannot be
        // built without them; the next broadcast goes out a second later.
        if (!pWorldSpace || !pTES)
            return;

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
            {
                // The mod manager owns the cell lookup and is null before the
                // engine builds it, so fall through to the same "no cell" path
                // the code already handles below.
                auto* pModManager = ModManager::Get();
                if (pModManager)
                    pCell = pModManager->GetCellFromCoordinates(gameCurrentGrid.X, gameCurrentGrid.Y, pWorldSpace, false);
            }

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
    catch (...)
    {
        Diagnostics::ReportFailure(__FUNCTION__, "ran out of memory in DiscoveryService::VisitExteriorCell");
    }
}

void DiscoveryService::VisitInteriorCell(bool aForceTrigger) noexcept
{
    // The LAN discovery responder, whose reports and peer list allocate. A reply that
    // could not be built is lost; the next broadcast goes out a second later.
    try
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
    catch (...)
    {
        Diagnostics::ReportFailure(__FUNCTION__, "ran out of memory in DiscoveryService::VisitInteriorCell");
    }
}

void DiscoveryService::DetectGridCellChange(TESWorldSpace* aWorldSpace, bool aNewCellGrid) noexcept
{
    // The LAN discovery responder, whose reports and peer list allocate. A reply that
    // could not be built is lost; the next broadcast goes out a second later.
    try
    {
        GridCellChangeEvent changeEvent{};

        const uint32_t worldSpaceId = aWorldSpace->formID;
        changeEvent.WorldSpaceId = worldSpaceId;

        const TES* pTES = TES::Get();

        // The game data is not up yet on the main menu and while a save loads, where
        // there are no centre coordinates to walk. This degrades the same way the
        // missing player cell below does: the change is warned about and dropped.
        if (pTES == nullptr)
        {
            spdlog::warn("Grid cell change for worldspace {:X} has no player cell to report", aWorldSpace->formID);
            return;
        }

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

                auto* pModManager = ModManager::Get();
                const TESObjectCELL* pCell = pModManager ? pModManager->GetCellFromCoordinates(startGridX + i, startGridY + j, aWorldSpace, 0) : nullptr;

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
        {
            auto* pModManager = ModManager::Get();
            if (pModManager)
                pCell = pModManager->GetCellFromCoordinates(pTES->currentGridX, pTES->currentGridY, aWorldSpace, false);
        }

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
    catch (...)
    {
        Diagnostics::ReportFailure(__FUNCTION__, "ran out of memory in DiscoveryService::DetectGridCellChange");
    }
}

void DiscoveryService::VisitForms() noexcept
{
    // The set and the enqueued events are the allocations, on the tick that walks
    // every loaded actor. A failure stops the visit for this tick: the actors not
    // visited are simply not discovered yet, and the next tick starts over from
    // m_forms, which still holds what was found.
    try
    {
        // Clear-and-refill rather than assign: assigning rebuilt the whole set every
        // tick, which on the game thread meant one allocation proportional to the
        // loaded-actor count 60-125 times a second. Clearing keeps the capacity, so
        // after the first few ticks this loop allocates nothing.
        m_previousForms.clear();
        for (uint32_t formId : m_forms)
            m_previousForms.insert(formId);

        const auto visitor = [this](TESObjectREFR* apReference)
        {
            const auto formId = apReference->formID;

            if (!m_forms.count(formId))
            {
                m_forms.insert(formId);

                m_dispatcher.enqueue(ActorAddedEvent(formId));
            }
            else
                m_previousForms.erase(formId);
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
        for (uint32_t formId : m_previousForms)
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
    catch (...)
    {
        Diagnostics::ReportFailure(__FUNCTION__, "ran out of memory in DiscoveryService::VisitForms");
        // The actors not visited this tick are not discovered yet. m_forms still
        // holds what was found, so the next tick resumes from there.
    }
}

void DiscoveryService::OnUpdate(const PreUpdateEvent& acUpdateEvent) noexcept
{
    // A quarter second is far below the rate at which the loaded actor set or the
    // grid centre can actually change, and twenty times less work than the frame
    // rate. Transitions that must not wait for it - connecting, finishing a load -
    // call VisitCell(true) directly and do not come through here.
    constexpr double kVisitInterval = 0.25;

    m_visitAccumulator += acUpdateEvent.Delta;
    if (m_visitAccumulator < kVisitInterval)
        return;

    m_visitAccumulator = 0.0;

    VisitCell();
    VisitForms();
}

void DiscoveryService::OnConnected(const ConnectedEvent& acEvent) noexcept
{
    // uGridsToLoad should always be 5, as this is what the server enforces
    auto* pCollection = INISettingCollection::Get();
    auto* pSetting = pCollection ? pCollection->GetSetting("uGridsToLoad:General") : nullptr;
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

    // Upstream raises a "your installation is not purely vanilla" error here and
    // shows it to the player. That check demands exactly the eight vanilla/AE
    // plugins, so on this fork it would fire for every player running a modpack -
    // which is the audience this repository exists for. The information is still
    // worth having when someone reports instability, so it is logged instead of
    // popped up.
    // Without a manager there is no list to compare against, so the vanilla check has
    // nothing to say; staying quiet is what the cell lookup above does too.
    const auto* pModManager = ModManager::Get();
    if (pModManager && !IsDefaultModlist(pModManager->mods))
        spdlog::info("Mod list is not the default one; this is expected when playing with a modpack");

    VisitCell(true);

    return BSTEventResult::kOk;
}

void DiscoveryService::ResetCachedCellData() noexcept
{
    m_worldSpaceId = 0;
    m_centerGrid.Reset();
    m_currentGrid.Reset();
}
