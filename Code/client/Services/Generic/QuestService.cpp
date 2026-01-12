#include <TiltedOnlinePCH.h>

#include <Events/ConnectedEvent.h>

#include <Services/QuestService.h>
#include <Services/ImguiService.h>

#include <PlayerCharacter.h>
#include "AI/Movement/PlayerControls.h"
#include <Forms/TESQuest.h>
#include <Games/TES.h>
#include <Games/Overrides.h>

#include <Events/EventDispatcher.h>

#include <Messages/RequestQuestUpdate.h>
#include <Messages/NotifyQuestUpdate.h>

static TESQuest* FindQuestByNameId(const String& name)
{
    auto& questRegistry = ModManager::Get()->quests;
    auto it = std::find_if(questRegistry.begin(), questRegistry.end(), [name](auto* it) { return std::strcmp(it->idName.AsAscii(), name.c_str()); });

    return it != questRegistry.end() ? *it : nullptr;
}

QuestService::QuestService(World& aWorld, entt::dispatcher& aDispatcher)
    : m_world(aWorld)
{
    m_joinedConnection = aDispatcher.sink<ConnectedEvent>().connect<&QuestService::OnConnected>(this);
    m_questUpdateConnection = aDispatcher.sink<NotifyQuestUpdate>().connect<&QuestService::OnQuestUpdate>(this);

    // A note about the Gameevents:
    // TESQuestStageItemDoneEvent gets fired to late, we instead use TESQuestStageEvent, because it responds immediately.
    // TESQuestInitEvent can be instead managed by start stop quest management.
    // bind game event listeners
    auto* pEventList = EventDispatcherManager::Get();
    pEventList->questStartStopEvent.RegisterSink(this);
    pEventList->questStageEvent.RegisterSink(this);
}

void QuestService::OnConnected(const ConnectedEvent&) noexcept
{
    // TODO: this should be followed with whatever the quest leader selected
    /*
    // deselect any active quests
    auto* pPlayer = PlayerCharacter::Get();
    for (auto& objective : pPlayer->objectives)
    {
        if (auto* pQuest = objective.instance->quest)
            pQuest->SetActive(false);
    }
    */
}

BSTEventResult QuestService::OnEvent(const TESQuestStartStopEvent* apEvent, const EventDispatcher<TESQuestStartStopEvent>*)
{
    if (ScopedQuestOverride::IsOverriden() || !m_world.Get().GetPartyService().IsInParty())
        return BSTEventResult::kOk;

    TESQuest* pQuest = Cast<TESQuest>(TESForm::GetById(apEvent->formId));
    if (pQuest == nullptr)
        return BSTEventResult::kOk;     // This shouldn't happen...

    // If we can't get the GameId we can't sync anyway.
    GameId Id;
    auto& modSys = m_world.GetModSystem();
    if (!modSys.GetServerModId(pQuest->formID, Id))
    {
        spdlog::info(__FUNCTION__ ": can't get gameId for formId {:X}, can't sync quest {} questStage {} questType {} name {}",
                     pQuest->formID, pQuest->IsStopped() ? "stop" : "start", pQuest->currentStage,
                     static_cast<std::underlying_type_t<TESQuest::Type>>(pQuest->type),
                     pQuest->fullName.value.AsAscii());
        return BSTEventResult::kOk;
    }

    if (IsNonSyncableQuest(pQuest))
        return BSTEventResult::kOk;

    if (pQuest->type == TESQuest::Type::None || pQuest->type == TESQuest::Type::Miscellaneous)
    {
        spdlog::info(__FUNCTION__ ": queuing type none/misc quest {} gameId {:X} questStage {} "
                                  "questType {} formId {:X} name {}",
                     pQuest->IsStopped() ? "stop" : "start", Id.LogFormat(), pQuest->currentStage,
                     static_cast<std::underlying_type_t<TESQuest::Type>>(pQuest->type), pQuest->formID,
                     pQuest->fullName.value.AsAscii());
    }


    spdlog::info(__FUNCTION__ ":  quest {} formId: {:X}, questStage: {}, questType: {}, name: {}",
                    pQuest->IsStopped() ? "stopped" : "started", 
                    pQuest->formID,
                    pQuest->currentStage, 
                    static_cast<std::underlying_type_t<TESQuest::Type>>(pQuest->type),
                    pQuest->fullName.value.AsAscii());

    m_world.GetRunner().Queue([&, formId = pQuest->formID, stageId = pQuest->currentStage,
                                stopped = pQuest->IsStopped(), type = pQuest->type]()
        {
            GameId Id;
            auto& modSys = m_world.GetModSystem();
            if (modSys.GetServerModId(formId, Id))
            {
                RequestQuestUpdate update;
                update.Id = Id;
                update.Stage = stageId;
                update.Status = stopped ? RequestQuestUpdate::Stopped : RequestQuestUpdate::Started;
                update.ClientQuestType = static_cast<std::underlying_type_t<TESQuest::Type>>(type); 
                m_world.GetTransport().Send(update);
            }
        });

    return BSTEventResult::kOk;
}

BSTEventResult QuestService::OnEvent(const TESQuestStageEvent* apEvent, const EventDispatcher<TESQuestStageEvent>*)
{
    if (ScopedQuestOverride::IsOverriden() || !m_world.Get().GetPartyService().IsInParty())
        return BSTEventResult::kOk;

    TESQuest* pQuest = Cast<TESQuest>(TESForm::GetById(apEvent->formId));
    if (pQuest == nullptr)
        return BSTEventResult::kOk; // This shouldn't happen...

    // If we can't get the GameId we can't sync anyway.
    GameId Id;
    auto& modSys = m_world.GetModSystem();
    if (!modSys.GetServerModId(pQuest->formID, Id))
    {
        spdlog::info(__FUNCTION__ ": can't get gameId for formId {:X}, can't sync questStage {} questType {} name {}",
                     pQuest->formID, pQuest->currentStage,
                     static_cast<std::underlying_type_t<TESQuest::Type>>(pQuest->type), 
                     pQuest->fullName.value.AsAscii());
        return BSTEventResult::kOk;
    }

    if (IsNonSyncableQuest(pQuest))
        return BSTEventResult::kOk;

    // Party leaders can always advance quests. Members can only advance quest stages when their controls are enabled
    // This prevents party members from interfering with cutscenes (which almost always freeze players,
    // and almost always end with a quest stage advance which can catch things up)
    const bool canAdvanceQuestStages =
        m_world.Get().GetPartyService().IsLeader() || PlayerControls::IsMovementControlsEnabled();

    if (!canAdvanceQuestStages)
    {
        spdlog::info(__FUNCTION__ ": quest update by member blocked while player controls disabled, "
                                  "quest gameId {:X} questStage {} questType {} formId {:X}, name {}",
                                  Id.LogFormat(), pQuest->currentStage,
                                  static_cast<std::underlying_type_t<TESQuest::Type>>(pQuest->type),
                                  pQuest->formID, pQuest->fullName.value.AsAscii());
        return BSTEventResult::kOk;
    }

    if (pQuest->type == TESQuest::Type::None || pQuest->type == TESQuest::Type::Miscellaneous)
    {
        spdlog::info(__FUNCTION__ ": queuing type none/misc quest update gameId {:X} questStage {} "
                                  "questType {} formId {:X} name {}",
                                  Id.LogFormat(), pQuest->currentStage,
                                  static_cast<std::underlying_type_t<TESQuest::Type>>(pQuest->type), pQuest->formID,
                                  pQuest->fullName.value.AsAscii());
    }


    spdlog::info(__FUNCTION__ ":  quest updated formId: {:X}, questStage: {}, questType: {}, name: {}",
                    pQuest->formID, pQuest->currentStage, static_cast<std::underlying_type_t<TESQuest::Type>>(pQuest->type), pQuest->fullName.value.AsAscii());

    m_world.GetRunner().Queue(
        [&, formId = apEvent->formId, stageId = apEvent->stageId, type = pQuest->type]()
        {
            GameId Id;
            auto& modSys = m_world.GetModSystem();
            if (modSys.GetServerModId(formId, Id))
            {
                RequestQuestUpdate update;
                update.Id = Id;
                update.Stage = stageId;
                update.Status = RequestQuestUpdate::StageUpdate;
                update.ClientQuestType = static_cast<std::underlying_type_t<TESQuest::Type>>(type);
                m_world.GetTransport().Send(update);
            }
        });

    return BSTEventResult::kOk;
}

void QuestService::OnQuestUpdate(const NotifyQuestUpdate& aUpdate) noexcept
{
    ModSystem& modSystem = World::Get().GetModSystem();
    uint32_t formId = modSystem.GetGameId(aUpdate.Id);
    TESQuest* pQuest = Cast<TESQuest>(TESForm::GetById(formId));
    if (!pQuest)
    {
        spdlog::error(__FUNCTION__ ": failed to find quest, gameId: {:X}, stage: {}", aUpdate.Id.LogFormat(), aUpdate.Stage);
        return;
    }

    if (pQuest->type == TESQuest::Type::None || pQuest->type == TESQuest::Type::Miscellaneous)
    {
        spdlog::info(__FUNCTION__ ": receiving type none/misc quest update gameId {:X} questStage {} questStatus {} questType {} formId {:X} name {}",
                     aUpdate.Id.LogFormat(), aUpdate.Stage, aUpdate.Status,
                     aUpdate.ClientQuestType, formId, pQuest->fullName.value.AsAscii());
    }

    bool bResult = false;
    bool bRunning = pQuest->getState() == TESQuest::State::Running;
    switch (aUpdate.Status)
    {
    case NotifyQuestUpdate::Started:
        if (bRunning)
        {
            spdlog::info(__FUNCTION__ ": suppressing duplicate quest start gameId: {:X}, questStage: {}, questStatus: {}, questType: {}, formId: {:X}, name: {}",
                         aUpdate.Id.LogFormat(), aUpdate.Stage, aUpdate.Status, aUpdate.ClientQuestType, formId, pQuest->fullName.value.AsAscii());
        }
        else
        {
            spdlog::info(__FUNCTION__ ":  quest started remotely gameId: {:X}, questStage: {}, questStatus: {}, questType: {}, formId: {:X}, name: {}",
                         aUpdate.Id.LogFormat(), aUpdate.Stage, aUpdate.Status, aUpdate.ClientQuestType, formId, pQuest->fullName.value.AsAscii());
            pQuest->ScriptSetStage(aUpdate.Stage);
            pQuest->SetActive(true);
        }
        bResult = true;
        spdlog::info("Remote quest started: {:X}, stage: {}", formId, aUpdate.Stage);
        break;

    case NotifyQuestUpdate::StageUpdate:
        spdlog::info(__FUNCTION__ ":  quest updated remotely gameId: {:X}, questStage: {}, questStatus: {}, questType: {}, formId: {:X}, name: {}",
                     aUpdate.Id.LogFormat(), aUpdate.Stage, aUpdate.Status, aUpdate.ClientQuestType, formId, pQuest->fullName.value.AsAscii());

        pQuest->ScriptSetStage(aUpdate.Stage);
        bResult = true;
        break;

    case NotifyQuestUpdate::Stopped:
        spdlog::info(__FUNCTION__ ":  quest stopped remotely gameId: {:X}, questStage: {}, questStatus: {}, questType: {}, formId: {:X}, name: {}",
                     aUpdate.Id.LogFormat(), aUpdate.Stage, aUpdate.Status, aUpdate.ClientQuestType, formId, pQuest->fullName.value.AsAscii());
        bResult = StopQuest(formId);
        break;
 
    default: break;
    }

    if (!bResult)
        spdlog::error(__FUNCTION__ ": failed to update the client quest state gameId: {:X}, questStage: {}, questStatus: {}, questType: {}, formId: {:X}, name: {}",
                      aUpdate.Id.LogFormat(), aUpdate.Stage, aUpdate.Status, aUpdate.ClientQuestType, formId, pQuest->fullName.value.AsAscii());
}

bool QuestService::StopQuest(uint32_t aformId)
{
    TESQuest* pQuest = Cast<TESQuest>(TESForm::GetById(aformId));
    if (pQuest)
    {
        if (pQuest->getState() == TESQuest::State::Stopped) // Supress duplicate or loopback quest stop
        {
            spdlog::info(__FUNCTION__ ": suppressing duplicate quest stop formId: {:X}, questStage: {}, questFlags: {:X}, questType: {}, formId: {:X}, name: {}",
                         aformId, pQuest->currentStage, static_cast<uint8_t>(pQuest->flags), static_cast<uint16_t>(pQuest->type), aformId, pQuest->fullName.value.AsAscii());
        }
        else
        {
            pQuest->SetActive(false);
            pQuest->SetStopped();
        }

        return true;
    }

    return false;
}

static constexpr std::array kNonSyncableQuestIds = std::to_array<uint32_t>({
    0x2BA16,   // Werewolf transformation quest
    0x20071D0, // Vampire transformation quest
    0x3AC44,   // MS13BleakFallsBarrowLeverScene
    // 0xFE014801,  // Unknown dynamic ID, kept as note, maybe lookup correct ID this game?
    0xF2593 // Skill experience quest
});

bool QuestService::IsNonSyncableQuest(TESQuest* apQuest)
{
    // Quests with no quest stages are never synced. Most TESQues::Type:: quests should
    // be synced, including Type::None and Type::Miscellaneous, but there are a few
    // known exceptions that should be excluded that are in the table.
    return    apQuest->stages.Empty() 
           || std::find(kNonSyncableQuestIds.begin(), kNonSyncableQuestIds.end(), apQuest->formID) != kNonSyncableQuestIds.end();
}

void QuestService::DebugDumpQuests()
{
    auto& quests = ModManager::Get()->quests;
    for (TESQuest* pQuest : quests)
        spdlog::info("{:X}|{}|{}|{}", pQuest->formID, (uint8_t)pQuest->type, pQuest->priority, pQuest->idName.AsAscii());
}
