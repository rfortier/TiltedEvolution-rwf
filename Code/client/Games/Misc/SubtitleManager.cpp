#include "SubtitleManager.h"

#include <Events/SubtitleEvent.h>

#include <TESObjectREFR.h>
#include <Games/ActorExtension.h>
#include <Forms/TESQuest.h>

#include <Forms/TESTopicInfo.h>
#include <Misc/BSFixedString.h>

SubtitleManager* SubtitleManager::Get() noexcept
{
    POINTER_SKYRIMSE(SubtitleManager*, s_singleton, 400443);
    return *s_singleton.Get();
}

TP_THIS_FUNCTION(TShowSubtitle, void, SubtitleManager, TESObjectREFR* apSpeaker, const char* apSubtitleText, bool aIsInDialogue);
static TShowSubtitle* RealShowSubtitle = nullptr;

void SubtitleManager::ShowSubtitle(TESObjectREFR* apSpeaker, const char* apSubtitleText, TESTopicInfo* apTopicInfo, bool aUnk1) noexcept
{
    TiltedPhoques::ThisCall(RealShowSubtitle, this, apSpeaker, apSubtitleText, aUnk1);
}

void* SubtitleManager::HideSubtitle(TESObjectREFR* apSpeaker) noexcept
{
    TP_THIS_FUNCTION(THideSubtitle, void*, SubtitleManager, TESObjectREFR* apSpeaker);
    POINTER_SKYRIMSE(THideSubtitle, s_hideSubtitle, 52627);
    return TiltedPhoques::ThisCall(s_hideSubtitle, this, apSpeaker);
}

void TP_MAKE_THISCALL(HookShowSubtitle, SubtitleManager, TESObjectREFR* apSpeaker, const char* apSubtitleText, bool aIsInDialogue)
{
    Actor* pActor = Cast<Actor>(apSpeaker);
    if (pActor)
    {  
        auto isLocal = pActor->GetExtension()->IsLocal();
        auto isLocalPlayer = pActor->GetExtension()->IsLocalPlayer();
        auto isRemoteInScene = !isLocal && pActor->GetCurrentScene() && pActor->GetCurrentScene()->isPlaying;
        bool shouldForward = apSubtitleText && (isLocal && !isLocalPlayer || isRemoteInScene);
        const bool isLeader = World::Get().GetPartyService().IsLeader(); // Helps distinguish logs in 2-party
        const auto pname = pActor->baseForm->GetName() ? pActor->baseForm->GetName() : "";

        if (shouldForward && isRemoteInScene)
            spdlog::debug(__FUNCTION__ ": forwarding subtitles because isRemoteInScene formId {:X} isLeader {} name {} text {}",
                          pActor->formID, isLeader, pname, apSubtitleText);

        if (shouldForward)
            World::Get().GetRunner().Trigger(SubtitleEvent(apSpeaker->formID, apSubtitleText));
    }

    TiltedPhoques::ThisCall(RealShowSubtitle, apThis, apSpeaker, apSubtitleText, aIsInDialogue);
}

static TiltedPhoques::Initializer s_subtitleHooks(
    []()
    {
        POINTER_SKYRIMSE(TShowSubtitle, s_showSubtitle, 52626);

        RealShowSubtitle = s_showSubtitle.Get();

        TP_HOOK(&RealShowSubtitle, HookShowSubtitle);
    });
