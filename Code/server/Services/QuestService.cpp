#include <GameServer.h>
#include <Components.h>

#include <World.h>
#include <Services/QuestService.h>
#include <Services/PartyService.h>

#include <Messages/RequestQuestUpdate.h>
#include <Messages/NotifyQuestUpdate.h>

#include <Setting.h>
namespace
{
Console::Setting bEnableMiscQuestSync{"Gameplay:bEnableMiscQuestSync", "(Experimental) Syncs miscellaneous quests when possible", false};
Console::Setting uQuestHistoryExpiration("GameServer:uQuestHistoryExpiration", "Time in milliseconds to retain quest progression changes for deduplication", 30000U);
}

QuestService::QuestService(World& aWorld, entt::dispatcher& aDispatcher)
    : m_world(aWorld)
{
    m_questUpdateConnection = aDispatcher.sink<PacketEvent<RequestQuestUpdate>>().connect<&QuestService::OnQuestChanges>(this);
}

void QuestService::OnQuestChanges(const PacketEvent<RequestQuestUpdate>& acMessage) noexcept
{
    const auto& message = acMessage.Packet;
    auto* pPlayer = acMessage.pPlayer;

    // We'll want to know the player party status for messaging & decisions
    // We want the leader to change things whenever possible, if a Member
    // advances a quest we can't undo it and need to remember.
    auto& partyService = m_world.GetPartyService();
    bool bInParty = partyService.IsPlayerInParty(pPlayer);
    bool bIsLeader = partyService.IsPlayerLeader(pPlayer);
    auto partyId = pPlayer->GetParty().JoinedPartyId;

    auto& questComponent = pPlayer->GetQuestLogComponent();
    auto& entries = questComponent.QuestContent.Entries;
    auto questIt = std::find_if(entries.begin(), entries.end(), [&message](const auto& e) { return e.Id == message.Id; });

    NotifyQuestUpdate notify{};
    notify.Id = message.Id;
    notify.Stage = message.Stage;
    // notify.Status = message.Status;  // This was a misleading bug. Was "accidentally correct" code.
    notify.SceneEndFlag = message.SceneEndFlag;
    notify.ClientQuestType = message.ClientQuestType;

    switch (message.Status)
    {
    case RequestQuestUpdate::Started:
    case RequestQuestUpdate::StageUpdate:
        // Update QuestComponent. In order to prevent bugs when 
        // we "discover" a quest in-progress (first seen in an 
        // update), we add it as a new quest record if not found
        if (questIt != entries.end())
        {
            questIt->Id = message.Id;
            questIt->Stage = message.Stage;
        }

        else
        {
            entries.emplace_back(message.Id, message.Stage);
            questIt = std::prev(entries.end());
            spdlog::info("{}: started quest: {:X}, stage: {}, by {} {:X}", __FUNCTION__, message.Id.LogFormat(),
                         notify.Stage, bIsLeader ? "leader" : "player", pPlayer->GetId());
        }
    }

    switch (message.Status)
    {
    case RequestQuestUpdate::Started:
        notify.Status = NotifyQuestUpdate::Started;
        break;

    case RequestQuestUpdate::StageUpdate:
        notify.Status = NotifyQuestUpdate::StageUpdate;
        spdlog::info("{}: updated quest: {:X}, stage: {}, sceneEndFlag {}, by {} {:X}", __FUNCTION__, message.Id.LogFormat(),
                     notify.Stage, notify.SceneEndFlag != 0, bIsLeader ? "leader" : "player", pPlayer->GetId());
        break;

    case RequestQuestUpdate::Stopped:
        notify.Status = NotifyQuestUpdate::Stopped;
        spdlog::info("{}: stopped quest: {:X}, stage: {}, by {} {:X}", __FUNCTION__, message.Id.LogFormat(),
                     notify.Stage, bIsLeader ? "leader" : "player", pPlayer->GetId());

        if (questIt != entries.end())
            entries.erase(questIt);
        else
        {
            spdlog::warn("{}: unable to delete quest object {:X} (already stopped or first update is stopped)", __FUNCTION__, notify.Id.LogFormat());
        } 
        break;
    }

    // All side effects have been generated. Now just logging and a forwarding decision left.
    if (bInParty)
    {
        if (notify.ClientQuestType == 0 || notify.ClientQuestType == 6) // Types None or Miscellaneous. Hard-coded to avoid client header file.
        {
            if (!bEnableMiscQuestSync)
                return;

            spdlog::info("{}: syncing type none/misc quest to party, quest: {:X}, questStage: {}, questStatus: {}, questType: {}",
                         __FUNCTION__, notify.Id.LogFormat(), notify.Stage, notify.Status, notify.ClientQuestType);
        }

        // Now that party MEMBERS can advance quests in addition to the party leader, must prevent loops/reflections.
        // If the sender of the request is a party Member, SendToLeader() if noone is in the QuestStageDedpHistory for 
        // this quest+stage, then add self to the dedup history.
        // 
        // If the requesting player is the party Leader, add self to the quest deduping history and SendToParty(). If 
        // the originator was a party member, they are already in the dedup history. SendToParty() skips sending to any 
        // party member who already has this quest+stage change.
        // 
        // If the SceneEndFlag is set, we want to kick the entire party so don't loop around through Leader,
        // pretend to be leader if the request came from a party Member.
        // 
        PartyService::Party* pParty = partyService.GetPlayerParty(pPlayer);
        Player* pLeader = bIsLeader ? pPlayer : m_world.GetPlayerManager().GetById(pParty->LeaderPlayerId);
        auto& dedupHistory = pLeader->GetQuestStageDedupHistory();


        if (bIsLeader || notify.SceneEndFlag)
        {
            // Leader originated or party member sent to leader. 
            // SendToParty unless Leader has already done it. 
            if (notify.SceneEndFlag == false && dedupHistory.FoundStageWPlayerId(notify.Id, notify.Stage, pPlayer->GetId()))
                spdlog::info("{}: SendToParty dropping duplicate: quest: {:X}, stage: {}, by {} {:X}", __FUNCTION__,
                             notify.Id.LogFormat(), notify.Stage, bIsLeader ? "leader" : "player", pPlayer->GetId());
            else
            {
                spdlog::info("{}: SendToParty: quest: {:X}, stage: {}, by {} {:X}", 
                             __FUNCTION__, notify.Id.LogFormat(), notify.Stage, bIsLeader ? "leader" : "player", pPlayer->GetId());

                dedupHistory.Add(notify.Id, notify.Stage, pPlayer->GetId());

                // We need our own loop since SendToParty can't decide which members already have updates
                for (Player* pPlayer : m_world.GetPlayerManager())
                {
                    const auto& partyComponent = pPlayer->GetParty();
                    if (partyComponent.JoinedPartyId == partyId)
                    {
                        if (dedupHistory.FoundStageWPlayerId(notify.Id, notify.Stage, pPlayer->GetId()))
                        {
                            spdlog::info("{}: SendToParty skipping duplicate send quest: {:X}, stage: {}, sceneEndFlag {}, to player {:X}",
                                         __FUNCTION__, notify.Id.LogFormat(), notify.Stage, notify.SceneEndFlag != 0, pPlayer->GetId());
                        }
                        else
                        {
                            spdlog::info("{}: SendToParty sending quest: {:X}, stage: {}, sceneEndFlag {}, to player {:X}", 
                                         __FUNCTION__, notify.Id.LogFormat(), notify.Stage, notify.SceneEndFlag != 0, pPlayer->GetId());
                            pPlayer->Send(notify);
                        }
                    }
                }
            }
        }

        else
        {
            // Party member advanced quest; forward just to party leader
            // But don't if someone already has!
            bool bFound = notify.SceneEndFlag == 0 && dedupHistory.FoundStage(notify.Id, notify.Stage);
            dedupHistory.Add(notify.Id, notify.Stage, pPlayer->GetId());  

            if (bFound)
                spdlog::info("{}: SendToLeader dropping duplicate quest: {:X}, stage: {}, by {} {:X}", __FUNCTION__,
                             notify.Id.LogFormat(), notify.Stage, bIsLeader ? "leader" : "player", pPlayer->GetId());
            else
            {
                spdlog::info("{}: SendToLeader quest: {:X}, stage: {}, by {} {:X}", __FUNCTION__,
                             notify.Id.LogFormat(), notify.Stage, bIsLeader ? "leader" : "player", pPlayer->GetId());

                GameServer::Get()->SendToLeader(notify, pPlayer->GetParty(), pLeader);
            }
        }
    }
}

void inline QuestStageDedupHistory::Expire()
{
    const auto expiration = std::chrono::steady_clock::now() - uQuestHistoryExpiration.value_as<std::chrono::milliseconds>();

    while (!m_Cache.empty() && m_Cache.front().timestamp < expiration)
    {
        auto& it = m_Cache.front();
        spdlog::info("{}: expiring dedup history entry quest: {:X}, stage: {}, by {:X}",
                     __FUNCTION__, it.questId.LogFormat(), it.questStage, it.playerId);
        m_Cache.pop_front();
    }
}


QuestStageDedupHistory::const_iterator QuestStageDedupHistory::FindStage(const QuestId& aQuestId, const QuestStage& aQuestStage)
{
    Expire();

    return std::find_if(m_Cache.begin(), m_Cache.end(), [&](const QuestStageDedupHistory::Entry& e) 
    {
        return e.questId == aQuestId && e.questStage == aQuestStage;
    });
}

QuestStageDedupHistory::const_iterator QuestStageDedupHistory::FindStageWPlayerId(const QuestId& aQuestId, const QuestStage& aQuestStage, const PlayerId& aPlayerId)
{
    Expire();

    return std::find_if(m_Cache.begin(), m_Cache.end(), [&](const QuestStageDedupHistory::Entry& e)
    {
        return e.questId == aQuestId && e.questStage == aQuestStage && aPlayerId == e.playerId;
    });
}

void QuestStageDedupHistory::Add(QuestId aQuestId, QuestStage aQuestStage, PlayerId aPlayerId, TimeStamp aTimeStamp)
{
    Expire();
    m_Cache.emplace_back(aQuestId, aQuestStage, aPlayerId, aTimeStamp);
}
