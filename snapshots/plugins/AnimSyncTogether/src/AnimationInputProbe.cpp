#include "AnimSyncTogether/AnimationInputProbe.h"

#include "AnimSyncTogether/AnimationClipProbe.h"
#include "AnimSyncTogether/GraphVariableSync.h"
#include "AnimSyncTogether/STRPMClient.h"
#include "AnimSyncTogether/SyncRules.h"

#include <string>

namespace AnimSyncTogether
{
    namespace
    {
        bool IsHelmetInput(std::string_view eventName)
        {
            return eventName == "OffsetGPMA" || eventName == "OffsetGPMAStop";
        }
    }

    void AnimationInputProbe::Install()
    {
        if (installed_) {
            return;
        }

        REL::Relocation<std::uintptr_t> playerGraphVTable{ RE::VTABLE_PlayerCharacter[3] };
        originalPlayerNotify_ = playerGraphVTable.write_vfunc(0x1, PlayerNotifyAnimationGraph);
        installed_ = true;

        SKSE::log::info("AnimationInputProbe: PlayerCharacter NotifyAnimationGraph hook installed");
    }

    bool AnimationInputProbe::PlayerNotifyAnimationGraph(
        RE::IAnimationGraphManagerHolder* holder,
        const RE::BSFixedString& eventName)
    {
        const std::string_view name = eventName.c_str();
        auto* rules = SyncRules::GetSingleton();
        const bool tracked = rules->ShouldSyncEvent(name);

        // Capture all explicitly profiled graph variables before every local graph
        // input. Changed values are sent on the same reliable ordered STRPM channel
        // as custom events, so a remote proxy receives state before a replayed event.
        GraphVariableSync::GetSingleton()->CaptureAndSend(holder);

        std::int32_t animationType = 0;
        std::int32_t offsetType = 0;
        bool hasAnimationType = false;
        bool hasOffsetType = false;

        // Keep the already-validated Helmet Toggle state embedded in its event
        // packets during the generic-engine transition. This remains a redundant
        // safety path while the new graph-variable transport is being validated.
        if (IsHelmetInput(name)) {
            const RE::BSFixedString animationTypeVariable{ "iGPMAAnimationType" };
            const RE::BSFixedString offsetTypeVariable{ "iGPMAOffsetType" };
            hasAnimationType = holder && holder->GetGraphVariableInt(animationTypeVariable, animationType);
            hasOffsetType = holder && holder->GetGraphVariableInt(offsetTypeVariable, offsetType);

            SKSE::log::info(
                "GPMAStateTx actor=00000014 event='{}' animationTypePresent={} animationType={} offsetTypePresent={} offsetType={}",
                name,
                hasAnimationType,
                animationType,
                hasOffsetType,
                offsetType);
        }

        if (tracked) {
            const std::string reason = "local profiled event " + std::string(name);
            AnimationClipProbe::ArmActor(0x14, reason);
        }

        const auto result = originalPlayerNotify_(holder, eventName);

        SKSE::log::info(
            "AnimInput actor=00000014 localPlayer=true event='{}' result={} tracked={}",
            name,
            result,
            tracked);

        // Only explicitly profiled events are replayed. Native locomotion, combat
        // and furniture events therefore remain owned by STR unless a compatibility
        // profile intentionally opts a specific event into AnimSync.
        if (result && tracked) {
            STRPMClient::GetSingleton()->SendAnimationEvent(
                name,
                {},
                animationType,
                IsHelmetInput(name) && hasAnimationType,
                offsetType,
                IsHelmetInput(name) && hasOffsetType);
        }

        return result;
    }
}
