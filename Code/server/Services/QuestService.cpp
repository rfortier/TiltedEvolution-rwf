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

}

QuestService::QuestService(World& aWorld, entt::dispatcher& aDispatcher)
    : m_world(aWorld)
{
    m_questUpdateConnection = aDispatcher.sink<PacketEvent<RequestQuestUpdate>>().connect<&QuestService::OnQuestChanges>(this);
}

#pragma optimize("", off)
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
    spdlog::info("{}: quest {:X} PlayerId {:X} bInParty {} bLeader {}", __FUNCTION__, message.Id.BaseId, pPlayer->GetId(), bInParty, bIsLeader);

    auto& questComponent = pPlayer->GetQuestLogComponent();
    auto& entries = questComponent.QuestContent.Entries;
    auto questIt = std::find_if(entries.begin(), entries.end(), [&message](const auto& e) { return e.Id == message.Id; });

    NotifyQuestUpdate notify{};
    notify.Id = message.Id;
    notify.Stage = message.Stage;
    // notify.Status = message.Status;  // This was a misleading bug.
    notify.ClientQuestType = message.ClientQuestType;

    switch (message.Status)
    {
    case RequestQuestUpdate::Started:
    case RequestQuestUpdate::StageUpdate:
        // in order to prevent bugs when a quest is in progress
        // and being updated we add it as a new quest record if
        // it wasn't found
        if (questIt == entries.end())
        {
            auto& newQuest = entries.emplace_back();
            newQuest.Id = message.Id;
            newQuest.Stage = message.Stage;
            questIt = std::prev(entries.end());

            spdlog::info("{}: started quest: {:X}, stage: {}, by {} {:X}", __FUNCTION__, message.Id.LogFormat(),
                         message.Stage, bIsLeader ? "leader" : "player", pPlayer->GetId());
        }
    }

    switch (message.Status)
    {
    case RequestQuestUpdate::Started:
        notify.Status = NotifyQuestUpdate::Started;
        break;

    case RequestQuestUpdate::StageUpdate:
        notify.Status = NotifyQuestUpdate::StageUpdate;
        spdlog::info("{}: updated quest: {:X}, stage: {}, by {} {:X}", __FUNCTION__, message.Id.LogFormat(),
                     message.Stage, bIsLeader ? "leader" : "player", pPlayer->GetId());
        break;

    case RequestQuestUpdate::Stopped:
        notify.Status = NotifyQuestUpdate::Stopped;
        spdlog::info("{}: stopped quest: {:X}, stage: {}, by {} {:X}", __FUNCTION__, message.Id.LogFormat(),
                     message.Stage, bIsLeader ? "leader" : "player", pPlayer->GetId());

        if (questIt != entries.end())
            entries.erase(questIt);
        else
        {
            spdlog::warn("{}: unable to delete quest object {:X} (already stopped?)", __FUNCTION__, message.Id.LogFormat());
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
        PartyService::Party* pParty = partyService.GetPlayerParty(pPlayer);
        Player* pLeader = bIsLeader ? pPlayer : m_world.GetPlayerManager().GetById(pParty->LeaderPlayerId);
        auto& dedupHistory = pLeader->GetQuestStageDedupHistory();


        if (bIsLeader)
        {
            // Leader originated or party member sent to leader. 
            // SendToParty unless Leader has already done it. 
            if (dedupHistory.FoundStageWPlayerId(notify.Id, notify.Stage, pPlayer->GetId()))
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
                            spdlog::info("{}: SendToParty skipping duplicate send quest: {:X}, stage: {}, to player {:X}",
                                         __FUNCTION__, notify.Id.LogFormat(), notify.Stage, pPlayer->GetId());
                        }
                        else
                        {
                            spdlog::info("{}: SendToParty sending quest: {:X}, stage: {}, to player {:X}", 
                                         __FUNCTION__, notify.Id.LogFormat(), notify.Stage, pPlayer->GetId());
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
            bool bFound = dedupHistory.FoundStage(notify.Id, notify.Stage);
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
    const auto expiration = std::chrono::steady_clock::now() - timeout;


    while (!m_Cache.empty() && m_Cache.front().timestamp < expiration)
    {
        auto& it = m_Cache.front();
        spdlog::info("{}: removing quest: {:X}, stage: {}, for {:X}", __FUNCTION__, it.questId.LogFormat(), it.questStage, it.playerId);
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

    spdlog::info("{}: adding quest: {:X}, stage: {}, for {:X}", __FUNCTION__, aQuestId.LogFormat(), aQuestStage, aPlayerId);
    m_Cache.emplace_back(aQuestId, aQuestStage, aPlayerId, aTimeStamp);
}
