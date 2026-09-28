#include "PCH.h"
#include "PapyrusConsentBridge.h"

#include "CoopSessionManager.h"

namespace OStimTogether::PapyrusConsentBridge
{
    namespace
    {
        std::int32_t BeginAddActorConsent(
            RE::StaticFunctionTag*,
            RE::BSFixedString selectedLabel)
        {
            const char* raw = selectedLabel.c_str();
            return CoopSessionManager::GetSingleton()
                .BeginAddActorConsent(raw ? std::string_view(raw) : std::string_view{});
        }

        std::int32_t PollAddActorConsent(
            RE::StaticFunctionTag*,
            std::int32_t gateID)
        {
            return CoopSessionManager::GetSingleton()
                .PollAddActorConsent(gateID);
        }
    }

    bool Register(RE::BSScript::IVirtualMachine* vm)
    {
        if (!vm) {
            return false;
        }

        vm->RegisterFunction(
            "BeginAddActorConsent",
            "OStimTogetherNative",
            BeginAddActorConsent);
        vm->RegisterFunction(
            "PollAddActorConsent",
            "OStimTogetherNative",
            PollAddActorConsent);

        SKSE::log::info(
            "OSTNET PAPYRUS CONSENT bridge READY class=OStimTogetherNative");
        return true;
    }
}
